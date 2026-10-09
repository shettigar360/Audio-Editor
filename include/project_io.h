#ifndef PROJECT_IO_H
#define PROJECT_IO_H

#include <windows.h>
#include <commdlg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "daw_types.h"

static inline bool open_generic_dialog(char *out_path, int max_len, const char *filter, bool is_save) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (is_save) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        if (GetSaveFileNameA(&ofn)) { strncpy(out_path, szFile, max_len); return true; }
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) { strncpy(out_path, szFile, max_len); return true; }
    }
    return false;
}

static inline void save_project_file(void) {
    ProjectTab *p = get_cur_tab();
    char path[MAX_PATH];
    if (open_generic_dialog(path, sizeof(path), "EHS DAW Project (*.ehs)\0*.ehs\0", true)) {
        if (!strstr(path, ".ehs")) strcat(path, ".ehs");
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(&p->num_tracks, sizeof(int), 1, f);
            fwrite(&p->num_items, sizeof(int), 1, f);
            fwrite(p->track_db, sizeof(float), MAX_TRACKS, f);
            fwrite(p->track_heights, sizeof(int), MAX_TRACKS, f);
            fwrite(p->track_names, sizeof(char) * 64, MAX_TRACKS, f);
            fwrite(&p->num_markers, sizeof(int), 1, f);
            fwrite(p->markers, sizeof(DawMarker), p->num_markers, f);
            for (int i = 0; i < p->num_items; i++) {
                fwrite(&p->items[i].track_idx, sizeof(int), 1, f);
                fwrite(&p->items[i].start_time, sizeof(double), 1, f);
                fwrite(&p->items[i].offset_in_file, sizeof(double), 1, f);
                fwrite(&p->items[i].duration, sizeof(double), 1, f);
                fwrite(p->items[i].file_path, 512, 1, f);
            }
            fclose(f);
            const char *fn = strrchr(path, '\\');
            if (!fn) fn = strrchr(path, '/');
            snprintf(p->project_name, sizeof(p->project_name), "%s", fn ? fn + 1 : path);
        }
    }
}

static inline void load_project_file(void) {
    ProjectTab *p = get_cur_tab();
    char path[MAX_PATH];
    if (open_generic_dialog(path, sizeof(path), "EHS DAW Project (*.ehs)\0*.ehs\0", false)) {
        FILE *f = fopen(path, "rb");
        if (f) {
            push_undo_state();
            fread(&p->num_tracks, sizeof(int), 1, f);
            fread(&p->num_items, sizeof(int), 1, f);
            fread(p->track_db, sizeof(float), MAX_TRACKS, f);
            fread(p->track_heights, sizeof(int), MAX_TRACKS, f);
            fread(p->track_names, sizeof(char) * 64, MAX_TRACKS, f);
            fread(&p->num_markers, sizeof(int), 1, f);
            fread(p->markers, sizeof(DawMarker), p->num_markers, f);
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                fread(&it->track_idx, sizeof(int), 1, f);
                fread(&it->start_time, sizeof(double), 1, f);
                fread(&it->offset_in_file, sizeof(double), 1, f);
                fread(&it->duration, sizeof(double), 1, f);
                fread(it->file_path, 512, 1, f);
                process_media_cache(it->file_path, it);
            }
            fclose(f);
            const char *fn = strrchr(path, '\\');
            if (!fn) fn = strrchr(path, '/');
            snprintf(p->project_name, sizeof(p->project_name), "%s", fn ? fn + 1 : path);
        }
    }
}

static inline void write_wav_header(FILE *f, int sample_rate, int channels, int bits_per_sample, int total_frames) {
    int byte_rate = sample_rate * channels * (bits_per_sample / 8);
    int block_align = channels * (bits_per_sample / 8);
    int subchunk2_size = total_frames * block_align;
    int chunk_size = 36 + subchunk2_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int subchunk1_size = 16;
    short audio_format = 1; // PCM
    short num_ch = channels;
    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&num_ch, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    short ba = block_align;
    short bps = bits_per_sample;
    fwrite(&ba, 2, 1, f);
    fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&subchunk2_size, 4, 1, f);
}

static inline void execute_render_to_file(void) {
    ProjectTab *p = get_cur_tab();
    RenderSettings *rs = &g_daw.render_settings;

    char final_path[MAX_PATH];
    const char *ext = (rs->format_idx == 0) ? ".wav" : ((rs->format_idx == 1) ? ".mp3" : ((rs->format_idx == 2) ? ".flac" : ".ogg"));
    
    char base_file[128];
    strncpy(base_file, strlen(rs->filename) > 0 ? rs->filename : "render", sizeof(base_file));

    snprintf(final_path, sizeof(final_path), "%s\\%s%s", rs->directory, base_file, ext);

    if (rs->auto_increment) {
        int counter = 1;
        while (GetFileAttributesA(final_path) != INVALID_FILE_ATTRIBUTES) {
            snprintf(final_path, sizeof(final_path), "%s\\%s_%02d%s", rs->directory, base_file, counter++, ext);
        }
    }

    FILE *out = fopen(final_path, "wb");
    if (!out) return;

    int sr = rs->sample_rate;
    int ch = rs->channels;
    int bits = (rs->bit_depth_idx == 0) ? 16 : 24;

    double render_dur = 0.0;
    for (int i = 0; i < p->num_items; i++) {
        double it_end = p->items[i].start_time + p->items[i].duration;
        if (it_end > render_dur) render_dur = it_end;
    }
    if (render_dur <= 0.0) render_dur = 5.0;

    int total_frames = (int)(render_dur * sr);
    write_wav_header(out, sr, ch, bits, total_frames);

    int chunk_frames = 1024;
    float mix_buf[1024 * 2];
    float item_chunk[1024 * 2];

    bool any_sound = false;

    for (int frame_idx = 0; frame_idx < total_frames; frame_idx += chunk_frames) {
        int frames_to_render = (frame_idx + chunk_frames > total_frames) ? (total_frames - frame_idx) : chunk_frames;
        memset(mix_buf, 0, sizeof(float) * frames_to_render * 2);

        double chunk_st = (double)frame_idx / sr;
        double chunk_et = chunk_st + ((double)frames_to_render / sr);

        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (!it->raw_file_handle) continue;
            if (p->tracks[it->track_idx].mute) continue;

            float gain = db_to_linear(p->track_db[it->track_idx]);
            double it_s = it->start_time;
            double it_e = it->start_time + it->duration;

            if (it_e > chunk_st && it_s < chunk_et) {
                int64_t sample_offset = (int64_t)(((chunk_st - it_s) + it->offset_in_file) * SAMPLE_RATE);
                if (sample_offset >= 0) {
                    fseek(it->raw_file_handle, sample_offset * sizeof(float) * 2, SEEK_SET);
                    size_t read_frames = fread(item_chunk, sizeof(float) * 2, frames_to_render, it->raw_file_handle);
                    for (size_t f = 0; f < read_frames; f++) {
                        mix_buf[f * 2]     += item_chunk[f * 2] * gain;
                        mix_buf[f * 2 + 1] += item_chunk[f * 2 + 1] * gain;
                    }
                }
            }
        }

        process_master_plugins(mix_buf, frames_to_render, &p->master_filter, &p->master_delay);

        for (int f = 0; f < frames_to_render; f++) {
            float sample_l = mix_buf[f * 2];
            float sample_r = mix_buf[f * 2 + 1];

            if (fabsf(sample_l) > 0.0001f || fabsf(sample_r) > 0.0001f) any_sound = true;

            if (ch == 1) {
                float mono = (sample_l + sample_r) * 0.5f;
                if (bits == 16) {
                    short s = (short)(fmaxf(-1.0f, fminf(1.0f, mono)) * 32767.0f);
                    fwrite(&s, 2, 1, out);
                } else {
                    int s = (int)(fmaxf(-1.0f, fminf(1.0f, mono)) * 8388607.0f);
                    fwrite(&s, 3, 1, out);
                }
            } else {
                if (bits == 16) {
                    short sl = (short)(fmaxf(-1.0f, fminf(1.0f, sample_l)) * 32767.0f);
                    short sr_s = (short)(fmaxf(-1.0f, fminf(1.0f, sample_r)) * 32767.0f);
                    fwrite(&sl, 2, 1, out);
                    fwrite(&sr_s, 2, 1, out);
                } else {
                    int sl = (int)(fmaxf(-1.0f, fminf(1.0f, sample_l)) * 8388607.0f);
                    int sr_s = (int)(fmaxf(-1.0f, fminf(1.0f, sample_r)) * 8388607.0f);
                    fwrite(&sl, 3, 1, out);
                    fwrite(&sr_s, 3, 1, out);
                }
            }
        }
    }

    fclose(out);

    if (rs->skip_silent && !any_sound) {
        DeleteFileA(final_path);
    }
}

#endif // PROJECT_IO_H

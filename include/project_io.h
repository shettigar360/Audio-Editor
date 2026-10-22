#ifndef PROJECT_IO_H
#define PROJECT_IO_H

#include <windows.h>
#include <commdlg.h>
#include <stdbool.h>
#include <stdio.h>
#include "daw_types.h"

static inline bool open_generic_dialog(char *out_path, int max_len, const char *filter, bool is_save) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    if (out_path && out_path[0]) {
        strncpy(szFile, out_path, sizeof(szFile) - 1);
        szFile[sizeof(szFile) - 1] = '\0';
    }
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

static inline void export_rendered_audio(const char *ext) {
    ProjectTab *p = get_cur_tab();
    char filter[128], path[MAX_PATH];
    snprintf(filter, sizeof(filter), "Audio Format (*.%s)\0*.%s\0", ext, ext);
    if (open_generic_dialog(path, sizeof(path), filter, true)) {
        FILE *out_f = fopen(path, "wb");
        if (out_f) {
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].raw_file_handle) {
                    fseek(p->items[i].raw_file_handle, 0, SEEK_SET);
                    float buf[4096];
                    size_t r;
                    while ((r = fread(buf, sizeof(float), 4096, p->items[i].raw_file_handle)) > 0) {
                        fwrite(buf, sizeof(float), r, out_f);
                    }
                    break;
                }
            }
            fclose(out_f);
        }
    }
}

static inline void export_batch_tracks(void) {
    ProjectTab *p = get_cur_tab();
    char dir[MAX_PATH];
    if (open_generic_dialog(dir, sizeof(dir), "Batch Stem Prefix\0*.*\0", true)) {
        for (int t = 0; t < p->num_tracks; t++) {
            char stem_path[MAX_PATH];
            snprintf(stem_path, sizeof(stem_path), "%s_Track_%d.raw", dir, t + 1);
            FILE *f_stem = fopen(stem_path, "wb");
            if (f_stem) {
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].track_idx == t && p->items[i].raw_file_handle) {
                        fseek(p->items[i].raw_file_handle, 0, SEEK_SET);
                        float buf[4096];
                        size_t r;
                        while ((r = fread(buf, sizeof(float), 4096, p->items[i].raw_file_handle)) > 0) {
                            fwrite(buf, sizeof(float), r, f_stem);
                        }
                    }
                }
                fclose(f_stem);
            }
        }
    }
}

#endif // PROJECT_IO_H

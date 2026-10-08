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
    char p[MAX_PATH];
    if (open_generic_dialog(p, sizeof(p), "DAW Project (*.dawproj)\0*.dawproj\0", true)) {
        FILE *f = fopen(p, "wb");
        if (f) {
            fwrite(&g_daw.num_tracks, sizeof(int), 1, f);
            fwrite(&g_daw.num_items, sizeof(int), 1, f);
            fwrite(g_daw.track_db, sizeof(float), MAX_TRACKS, f);
            for (int i = 0; i < g_daw.num_items; i++) {
                fwrite(&g_daw.items[i].track_idx, sizeof(int), 1, f);
                fwrite(&g_daw.items[i].start_time, sizeof(double), 1, f);
                fwrite(&g_daw.items[i].offset_in_file, sizeof(double), 1, f);
                fwrite(&g_daw.items[i].duration, sizeof(double), 1, f);
                fwrite(g_daw.items[i].file_path, 512, 1, f);
            }
            fclose(f);
        }
    }
}

static inline void load_project_file(void) {
    char p[MAX_PATH];
    if (open_generic_dialog(p, sizeof(p), "DAW Project (*.dawproj)\0*.dawproj\0", false)) {
        FILE *f = fopen(p, "rb");
        if (f) {
            push_undo_state();
            fread(&g_daw.num_tracks, sizeof(int), 1, f);
            fread(&g_daw.num_items, sizeof(int), 1, f);
            fread(g_daw.track_db, sizeof(float), MAX_TRACKS, f);
            for (int i = 0; i < g_daw.num_items; i++) {
                AudioItem *it = &g_daw.items[i];
                fread(&it->track_idx, sizeof(int), 1, f);
                fread(&it->start_time, sizeof(double), 1, f);
                fread(&it->offset_in_file, sizeof(double), 1, f);
                fread(&it->duration, sizeof(double), 1, f);
                fread(it->file_path, 512, 1, f);
                process_media_cache(it->file_path, it);
            }
            fclose(f);
        }
    }
}

static inline void export_rendered_audio(const char *ext) {
    char filter[128], path[MAX_PATH];
    snprintf(filter, sizeof(filter), "Audio Format (*.%s)\0*.%s\0", ext, ext);
    if (open_generic_dialog(path, sizeof(path), filter, true)) {
        FILE *out_f = fopen(path, "wb");
        if (out_f) {
            for (int i = 0; i < g_daw.num_items; i++) {
                if (g_daw.items[i].raw_file_handle) {
                    fseek(g_daw.items[i].raw_file_handle, 0, SEEK_SET);
                    float buf[4096];
                    size_t r;
                    while ((r = fread(buf, sizeof(float), 4096, g_daw.items[i].raw_file_handle)) > 0) {
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
    char dir[MAX_PATH];
    if (open_generic_dialog(dir, sizeof(dir), "Batch Stem Prefix\0*.*\0", true)) {
        for (int t = 0; t < g_daw.num_tracks; t++) {
            char stem_path[MAX_PATH];
            snprintf(stem_path, sizeof(stem_path), "%s_Track_%d.raw", dir, t + 1);
            FILE *f_stem = fopen(stem_path, "wb");
            if (f_stem) {
                for (int i = 0; i < g_daw.num_items; i++) {
                    if (g_daw.items[i].track_idx == t && g_daw.items[i].raw_file_handle) {
                        fseek(g_daw.items[i].raw_file_handle, 0, SEEK_SET);
                        float buf[4096];
                        size_t r;
                        while ((r = fread(buf, sizeof(float), 4096, g_daw.items[i].raw_file_handle)) > 0) {
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

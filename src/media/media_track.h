
void mp_track_mgr_init(media_pipe_t *mp, media_track_mgr_t *mtm,
                       prop_t *root, int type, prop_t *current);

void mp_track_mgr_destroy(media_track_mgr_t *mtm);

void mp_track_mgr_next_track(media_track_mgr_t *mtm);

int mp_track_mgr_select_track(media_track_mgr_t *mtm,
                              event_select_track_t *est);

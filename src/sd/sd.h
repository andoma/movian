
prop_t *sd_add_service(const char *id, const char *title,
		       const char *icon, prop_t **status);

prop_t *sd_add_link(prop_t *svc, const char *title, const char *url);


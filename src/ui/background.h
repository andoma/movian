#pragma once

void background_init(prop_t *ui, prop_t *nav,
		     void (*set_image)(rstr_t *url, const char **vpaths,
				       void *opaque),
		     void (*set_alpha)(float alpha, void *opaque),
		     void *opaque);

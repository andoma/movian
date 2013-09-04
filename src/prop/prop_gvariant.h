#pragma once

#include <glib-object.h>

#include "prop.h"

void prop_set_from_gvariant(GVariant *v, prop_t *p);

void prop_set_from_vardict(GVariant *v, prop_t *parent);

void prop_set_from_tuple(GVariant *v, prop_t *parent);

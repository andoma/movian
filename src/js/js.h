#ifndef JS_H__
#define JS_H__


#include "showtime.h"
#include "prop.h"
#include "ext/spidermonkey/jsapi.h"

JSObject *js_object_from_prop(JSContext *cx, prop_t *p);


#endif // JS_H__ 

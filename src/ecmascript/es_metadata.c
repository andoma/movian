#include <assert.h>

#include "misc/rstr.h"

#include "ecmascript.h"
#include "metadata/metadata.h"
#include "metadata/metadata_str.h"
#include "prop/prop.h"

static int
es_video_metadata_bind_duk(duk_context *ctx)
{
  prop_t *root = es_stprop_get(ctx, 0);
  const char *urlstr = duk_safe_to_string(ctx, 1);

  rstr_t *url = rstr_alloc(urlstr);
  rstr_t *title;
  rstr_t *filename = es_prop_to_rstr(ctx, 2, "filename");
  int year         = es_prop_to_int(ctx, 2, "year", 0);

  if(filename != NULL) {
    // Raw filename case
    title = metadata_remove_postfix_rstr(filename);
    rstr_release(filename);
    year = -1;
  } else {
    title = es_prop_to_rstr(ctx, 2, "title");
  }

  int season    = es_prop_to_int(ctx,  2, "season", -1);
  int episode   = es_prop_to_int(ctx,  2, "episode", -1);
  rstr_t *imdb  = es_prop_to_rstr(ctx, 2, "imdb");
  int duration  = es_prop_to_int(ctx,  2, "duration", 0);

  metadata_lazy_video_t *mlv =
    metadata_bind_video_info(url, title, imdb, duration, root, NULL, 0, 0,
                             year, season, episode, 0);
  rstr_release(title);
  rstr_release(url);
  duk_push_pointer(ctx, mlv);
  return 1;
}


/**
 *
 */
static int
es_video_metadata_unbind_duk(duk_context *ctx)
{
  mlv_unbind(duk_require_pointer(ctx, 0), 0);
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_metadata[] = {
  { "videoMetadataBind",     es_video_metadata_bind_duk,       3 },
  { "videoMetadataUnbind",   es_video_metadata_unbind_duk,     1 },
  { NULL, NULL, 0}
};

/* -*- c-basic-offset: 2; indent-tabs-mode: nil -*- */
#ifndef DVDREAD_INTERNAL_H
#define DVDREAD_INTERNAL_H


#define CHECK_VALUE(arg)                                                \
  if(!(arg)) {                                                          \
    fprintf(stderr, "\n*** libdvdread: CHECK_VALUE failed in %s:%i ***" \
            "\n*** for %s ***\n\n",                                     \
            __FILE__, __LINE__, # arg );                                \
  }


int get_verbose(void);
int dvdread_verbose(dvd_reader_t *dvd);
dvd_reader_t *device_of_file(dvd_file_t *file);

#endif /* DVDREAD_INTERNAL_H */

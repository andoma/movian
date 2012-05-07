
CREATE TABLE videogenre (
       videoitem_id INTEGER REFERENCES videoitem(id) ON DELETE CASCADE,
       title TEXT,
       UNIQUE (videoitem_id, title)
);

CREATE INDEX videogenre_videoitem_id_idx ON videogenre(videoitem_id);

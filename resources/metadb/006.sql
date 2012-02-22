
-- Recreate videoitem

DROP TABLE videoitem;

CREATE TABLE "videoitem"(
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       ds_id INTEGER REFERENCES datasource(id) ON DELETE CASCADE,
       title TEXT,
       duration INTEGER,
       format TEXT,
       year INTEGER,
       description TEXT,
       rating FLOAT,
       season TEXT,
       episode TEXT
       );

CREATE TABLE url (
       id INTEGER PRIMARY KEY,
       url TEXT NOT NULL UNIQUE
       );

CREATE INDEX url_url_idx ON url(url);

CREATE TABLE page_kv (
       url_id INTEGER REFERENCES url(id) ON DELETE CASCADE,
       key TEXT NOT NULL,
       value,
       UNIQUE (url_id, key));

CREATE INDEX page_kv_url_id_idx ON page_kv(url_id);

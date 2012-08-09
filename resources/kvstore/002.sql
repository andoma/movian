DROP TABLE page_kv;

CREATE TABLE url_kv (
       url_id INTEGER REFERENCES url(id) ON DELETE CASCADE,
       domain INTEGER NOT NULL,
       key TEXT NOT NULL,
       value,
       UNIQUE (url_id, domain, key));

CREATE INDEX url_kv_url_id_idx ON url_kv(url_id);


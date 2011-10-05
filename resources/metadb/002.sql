
CREATE TABLE imageitem (
       item_id INTEGER UNIQUE REFERENCES item(id) ON DELETE CASCADE,
       original_time INTEGER
);
CREATE INDEX imageitem_item_id_idx ON imageitem(item_id);


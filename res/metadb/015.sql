
ALTER TABLE videoitem ADD COLUMN parent_id INTEGER REFERENCES videoitem(id) ON DELETE CASCADE;
ALTER TABLE videoitem ADD COLUMN idx;

CREATE INDEX videoitem_parent_id_idx ON videoitem(parent_id);

ALTER TABLE videoart ADD COLUMN weight;
ALTER TABLE videoart ADD COLUMN grp;
ALTER TABLE videoart ADD COLUMN titled;

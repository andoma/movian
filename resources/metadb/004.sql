-- Add new field 'title' to stream and drop everything from videoitem
-- videoitem can be completly reconstructed from the file data

ALTER TABLE stream ADD COLUMN title TEXT;
DELETE FROM videoitem;

#pragma once

/**
 * NBT Header
 */
typedef struct {
  uint8_t msg;
  uint8_t flags;
  uint16_t length;
} __attribute__((packed)) NBT_t;



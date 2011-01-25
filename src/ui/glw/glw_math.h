#pragma once

int glw_mtx_invert(float *dst, const float *src);

void glw_mtx_trans_mul_vec4(float *dst, const float *mt,
			    float x, float y, float z, float w);

void glw_mtx_mul_vec(float *dst, const float *mt, float x, float y, float z);

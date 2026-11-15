/*
 * The ABI tour of the C boundary app (doc/app_boundary W4a): one call of
 * each kind of argument and result the boundary rules give -- integers, a
 * String, a Symbol choice, a Float, bytes, a record, an array of records, a
 * record result -- and the results fed into later calls, so that a host
 * whose stand-in bodies hash every argument as it arrives
 * (fmrb_boundary_stub.inc) gets the same hash from the wasm app as from the
 * same calls made natively. Include fmrb_boundary.h (the SDK's in the app,
 * the firmware's in the host) before this.
 */
#ifndef C_BOUNDARY_TOUR_H
#define C_BOUNDARY_TOUR_H

static void
boundary_tour(void)
{
    static const char text[] = "boundary";
    static const char path[] = "/data/pic.png";
    static const uint8_t mask[] = { 0x81, 0x42, 0x24, 0x18 };
    fmrb_gfx_composite_region_t regions[2] = {
        { 1, 2, 3, 4, 5, 6, 1 },
        { -7, -8, 9, 10, 11, 12, 0 },
    };
    fmrb_gfx_image_info_t info = { 0, 0, 0 };
    fmrb_gfx_file_info_t st = { 0, 0 };
    int32_t r;

    fmrb_gfx_clear(0, 0x12);
    fmrb_gfx_draw_text_raw(0, 3, -4, text, (int32_t)(sizeof(text) - 1), 0xff, -1, 1);
    fmrb_gfx_set_font_raw(0, FMRB_GFX_SET_FONT_RAW_FAMILY_JA, 12);
    fmrb_gfx_draw_image_raw(0, 7, 8, 9, 1.5, 0.25);
    fmrb_gfx_create_mask(0, 8, 4, mask, (int32_t)sizeof(mask));
    fmrb_gfx_set_composite_regions_raw(0, regions, 2);
    r = fmrb_gfx_create_image(0, path, (int32_t)(sizeof(path) - 1), &info);
    fmrb_gfx_set_pixel(0, r, info.id, info.width);
    r = fmrb_gfx_file_status(0, path, (int32_t)(sizeof(path) - 1), &st);
    fmrb_gfx_set_pixel(0, r, st.exists, (int32_t)st.size);
    fmrb_gfx_present_raw(0, 0, 0, 0);
    fmrb_gfx_rgb_to_332(255, 128, 0);
    /* A pointer outside the app's memory: the wasm glue refuses it before the
       body runs (FMRB_BOUNDARY_ERR_ARG); in the host the answer is written in. */
#if defined(BOUNDARY_TOUR_IN_APP)
    r = fmrb_gfx_draw_text_raw(0, 0, 0, (const char *)0xfffffff0u, 16, 0, -1, 0);
#else
    r = FMRB_BOUNDARY_ERR_ARG;
#endif
    fmrb_gfx_set_pixel(0, r, 0, 0);
}

#endif /* C_BOUNDARY_TOUR_H */

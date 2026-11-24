#ifndef XIAOZHI_IMU_SCREEN_H
#define XIAOZHI_IMU_SCREEN_H

#include "graphics/renderer.h"

typedef struct {
    int32_t accel_mg[3], gyro_mdps[3];
    unsigned status;
    unsigned model;
} xz_imu_view_t;

enum {
    XZ_IMU_UNKNOWN = 0,
    XZ_IMU_MPU6050 = 0x68,
    XZ_IMU_MPU6500 = 0x70
};

enum {
    XZ_IMU_WAIT = 0,
    XZ_IMU_LIVE = 1,
    XZ_IMU_MISSING = 2,
    XZ_IMU_ERROR = 3,
    XZ_IMU_STALE = 4
};

/* Pure synchronous RGB565 renderer; no bus, task, logging or retained state.
 * Both snapshots must have a defined status/model and ALL components in range:
 * acceleration +/-2000 mg, angular velocity +/-250200 mdps (not angles).
 * Invalid input (including INT32_MIN) returns ROBOT_RENDER_INVALID BEFORE IO;
 * publish ERROR with in-range components to display error placeholders.
 * Non-LIVE snapshots never show measurements. Gyro rounds to nearest 0.1 dps,
 * ties away from zero, with unsigned zero (rendered +0.0, never -0.0).
 * previous == NULL forces ALL ten small fields, black background included.
 * Otherwise only fields with changed DISPLAY TEXT are sent, including black
 * pixels to erase the old text. UNKNOWN shows MPU; changing only model redraws
 * only the title. Canvas must be 360x360, capacity >=168 pixels.
 * Each rectangle is <=168 wide, each strip <=8 rows, using canvas->buffer.
 * Title (96,64,168,24); units (12/180,108,168,16);
 * XYZ (12/180,140/184/228,168,24); status (96,274,168,16).
 * Nothing at y300..324 is touched. Caller clears other page content once.
 * Caller owns stable snapshots and advances previous ONLY on success; after
 * a partial sink failure retry with previous=NULL (force), or the same pair.
 * Sink errors are returned unchanged immediately. No shared cache is used.
 */
int xz_imu_screen_render(robot_canvas_t *canvas, const xz_imu_view_t *view,
                         const xz_imu_view_t *previous);

#endif
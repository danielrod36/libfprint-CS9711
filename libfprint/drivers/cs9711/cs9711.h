/*
 * Chipsailing CS9711Fingprint driver
 *
 * Modified based on driver vfs301* so keeping original notice:
 *
 * Copyright (c) 2011-2012 Andrej Krutak <dev@andree.sk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include "fpi-usb-transfer.h"
#include "fpi-image-device.h"

#define CS9711_WIDTH 68
#define CS9711_HEIGHT 118

#define CS9711_FRAME_SIZE (CS9711_WIDTH * CS9711_HEIGHT)

typedef enum
{
  CS9711_SCAN_ERROR_COMPLETE,
  CS9711_SCAN_ERROR_FAIL,
} Cs9711ScanErrorAction;

typedef enum
{
  CS9711_DEACTIVATE_COMPLETE,
  CS9711_DEACTIVATE_WAIT_FOR_SCAN,
} Cs9711DeactivateAction;

typedef enum
{
  CS9711_INIT_MISMATCH_RESYNC,
  CS9711_INIT_MISMATCH_FAIL,
} Cs9711InitMismatchAction;

/* Init state machine states. Exposed so the ordering that the resync logic
 * depends on (recovery states before the status read, status read last) is
 * assertable in tests rather than only in comments. */
typedef enum
{
  CS9711_INIT_STATE_SEND_INI_QUERY = 0,
  CS9711_INIT_STATE_RECOVER_READ_IGNORED,
  CS9711_INIT_STATE_RECOVER_SEND_RESET,
  CS9711_INIT_STATE_RECOVER_READ_IGNORED_RESET,
  CS9711_INIT_STATE_RECOVER_SEND_INIT,
  CS9711_INIT_STATE_RESYNC_DRAIN,
  CS9711_INIT_STATE_RESYNC_SEND_RESET,
  CS9711_INIT_STATE_RESYNC_SETTLE,
  CS9711_INIT_STATE_RESYNC_SEND_INIT,
  CS9711_INIT_STATE_RECEIVE_STATUS,
  CS9711_INIT_STATE_COUNT,
} Cs9711InitState;

Cs9711ScanErrorAction    cs9711_scan_error_action    (const GError *error,
                                                      gboolean      action_cancelled);
Cs9711DeactivateAction   cs9711_deactivate_action    (gboolean scan_running);
Cs9711InitMismatchAction cs9711_init_mismatch_action (guint         resyncs,
                                                      guint         max_resyncs);

/* Successor of a synchronous init state. The init case bodies advance by
 * exactly one call to this, so the transition graph is data that tests can
 * assert instead of statements buried in a switch. Returns
 * CS9711_INIT_STATE_COUNT for a state that does not advance synchronously
 * (it defers to an async read callback). */
Cs9711InitState          cs9711_init_next_state     (Cs9711InitState state);

gboolean                 cs9711_init_error_is_recoverable (const GError *error);

typedef enum
{
  CS9711_INIT_EPILOGUE_ADVANCE, /* advance exactly one state */
  CS9711_INIT_EPILOGUE_FAIL,    /* fail the machine with the error */
  CS9711_INIT_EPILOGUE_STAY,    /* a state or callback owns the next step */
} Cs9711InitEpilogue;

Cs9711InitEpilogue       cs9711_init_epilogue       (gboolean has_error,
                                                      gboolean advanced_by_callback,
                                                      gboolean advanced_in_case);

struct _FpDeviceCs9711
{
  FpImageDevice parent;

  unsigned char image_buffer[CS9711_FRAME_SIZE];
  gboolean      scan_running;
  gboolean      scan_started;
  gboolean      image_ready;
  gboolean      deactivating;
  guint         init_resyncs;
};

G_DECLARE_FINAL_TYPE (FpDeviceCs9711, fpi_device_cs9711, FPI, DEVICE_CS9711, FpImageDevice)

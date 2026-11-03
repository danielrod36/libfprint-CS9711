/*
 * Copyright (C) 2026 AkosCz
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <glib.h>

#include "drivers/cs9711/cs9711.h"

static void
test_scan_error_action (void)
{
  g_autoptr(GError) cancelled = NULL;
  g_autoptr(GError) disconnected = NULL;

  cancelled = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                   "cancelled");
  disconnected = g_error_new_literal (G_USB_DEVICE_ERROR,
                                      G_USB_DEVICE_ERROR_NO_DEVICE,
                                      "disconnected");

  g_assert_cmpint (cs9711_scan_error_action (cancelled, TRUE), ==,
                   CS9711_SCAN_ERROR_COMPLETE);
  g_assert_cmpint (cs9711_scan_error_action (cancelled, FALSE), ==,
                   CS9711_SCAN_ERROR_FAIL);
  g_assert_cmpint (cs9711_scan_error_action (disconnected, TRUE), ==,
                   CS9711_SCAN_ERROR_FAIL);
}

static void
test_deactivate_action (void)
{
  g_assert_cmpint (cs9711_deactivate_action (TRUE), ==,
                   CS9711_DEACTIVATE_WAIT_FOR_SCAN);
  g_assert_cmpint (cs9711_deactivate_action (FALSE), ==,
                   CS9711_DEACTIVATE_COMPLETE);
}

/* The resync budget is per activation: the first N mismatches resync, then the
 * device is declared dead. Getting the comparison wrong either retries
 * forever or gives up on the first transient mismatch. */
static void
test_init_mismatch_action (void)
{
  const guint max = 2;

  g_assert_cmpint (cs9711_init_mismatch_action (0, max), ==,
                   CS9711_INIT_MISMATCH_RESYNC);
  g_assert_cmpint (cs9711_init_mismatch_action (1, max), ==,
                   CS9711_INIT_MISMATCH_RESYNC);
  g_assert_cmpint (cs9711_init_mismatch_action (2, max), ==,
                   CS9711_INIT_MISMATCH_FAIL);
  g_assert_cmpint (cs9711_init_mismatch_action (3, max), ==,
                   CS9711_INIT_MISMATCH_FAIL);
}

/* The SSM runs states in order and completes when cur_state reaches COUNT, so
 * the status read must be the last state. If a resync state were appended after
 * it, that state would run on the success path too, and anything between the
 * status read and COUNT would be skipped only in the failing case. A test can
 * pin the invariant; a comment cannot. */
static void
test_init_state_order (void)
{
  g_assert_cmpint (CS9711_INIT_STATE_COUNT, ==,
                   CS9711_INIT_STATE_RECEIVE_STATUS + 1);
  g_assert_cmpint (CS9711_INIT_STATE_RESYNC_DRAIN, <,
                   CS9711_INIT_STATE_RECEIVE_STATUS);
  g_assert_cmpint (CS9711_INIT_STATE_RESYNC_SEND_INIT + 1, ==,
                   CS9711_INIT_STATE_RECEIVE_STATUS);
  g_assert_cmpint (CS9711_INIT_STATE_RECOVER_SEND_INIT + 1, ==,
                   CS9711_INIT_STATE_RESYNC_DRAIN);
  g_assert_cmpint (CS9711_INIT_STATE_SEND_INI_QUERY, ==, 0);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/cs9711/scan-error-action", test_scan_error_action);
  g_test_add_func ("/cs9711/deactivate-action", test_deactivate_action);
  g_test_add_func ("/cs9711/init-mismatch-action", test_init_mismatch_action);
  g_test_add_func ("/cs9711/init-state-order", test_init_state_order);

  return g_test_run ();
}

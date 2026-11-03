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

/* The transitions the two review rounds actually fixed. Reading them from
 * cs9711_init_next_state() means the assertions break if either successor is
 * reverted, which the layout assertions above could not detect. */
static void
test_init_transitions (void)
{
  /* F2: recovery keeps its pre-resync successor. RESYNC_DRAIN here would
   * drain and reset a device that just accepted the re-sent init query. */
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RECOVER_SEND_INIT), ==,
                   CS9711_INIT_STATE_RECEIVE_STATUS);
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RECOVER_SEND_RESET), ==,
                   CS9711_INIT_STATE_RECOVER_READ_IGNORED_RESET);

  /* F1: the resync path advances one state at a time, in order, and reaches
   * RECEIVE_STATUS exactly once. The former defect stepped twice out of
   * RESYNC_SEND_INIT; walking the table makes a double step visible, because
   * RECEIVE_STATUS must be reachable from RESYNC_SEND_INIT in one hop and
   * must not itself advance synchronously. */
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RESYNC_DRAIN), ==,
                   CS9711_INIT_STATE_RESYNC_SEND_RESET);
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RESYNC_SETTLE), ==,
                   CS9711_INIT_STATE_RESYNC_SEND_INIT);
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RESYNC_SEND_INIT), ==,
                   CS9711_INIT_STATE_RECEIVE_STATUS);
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RECEIVE_STATUS), ==,
                   CS9711_INIT_STATE_COUNT);

  /* A state that defers to a read callback must not claim a synchronous
   * successor, and must never read as "complete". */
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RECOVER_READ_IGNORED), ==,
                   CS9711_INIT_STATE_COUNT);
  g_assert_cmpint (cs9711_init_next_state (CS9711_INIT_STATE_RECOVER_READ_IGNORED_RESET), ==,
                   CS9711_INIT_STATE_COUNT);

  /* The whole resync chain from the mismatch jump target is terminal at
   * RECEIVE_STATUS: no successor of any resync state can skip past it. */
  Cs9711InitState s = CS9711_INIT_STATE_RESYNC_DRAIN;
  guint hops = 0;
  while (s != CS9711_INIT_STATE_COUNT && s != CS9711_INIT_STATE_RECEIVE_STATUS)
    {
      s = cs9711_init_next_state (s);
      hops++;
      g_assert_cmpuint (hops, <=, CS9711_INIT_STATE_COUNT);
    }
  g_assert_cmpint (s, ==, CS9711_INIT_STATE_RECEIVE_STATUS);
  g_assert_cmpuint (hops, ==, 4); /* DRAIN, SEND_RESET, SETTLE, SEND_INIT */
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/cs9711/scan-error-action", test_scan_error_action);
  g_test_add_func ("/cs9711/deactivate-action", test_deactivate_action);
  g_test_add_func ("/cs9711/init-mismatch-action", test_init_mismatch_action);
  g_test_add_func ("/cs9711/init-state-order", test_init_state_order);
  g_test_add_func ("/cs9711/init-transitions", test_init_transitions);

  return g_test_run ();
}

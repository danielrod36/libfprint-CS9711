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

  /* F1: the resync path advances one state at a time and reaches
   * RECEIVE_STATUS exactly once, then stops. What this pins is the graph:
   * RECEIVE_STATUS has no synchronous successor and the chain from the mismatch
   * jump target is exactly four hops. The double advance that caused the
   * blocker was an extra statement in a case body, which no table assertion can
   * see; m_init_state() now advances only from its epilogue, so there is no
   * per-case advance for that defect to reappear in. */
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

/* The round-3 blocker: the init-timeout path advanced the machine and then let
 * its (recovered) error reach the epilogue, which failed the machine while the
 * follow-up bulk read was in flight. The rule is that a state which already
 * advanced owns the next step, so the epilogue must never fail. */
static void
test_init_epilogue (void)
{
  Cs9711InitEpilogue e;
  /* Clean sync state: advance. */
  g_assert_cmpint (cs9711_init_epilogue (FALSE, FALSE, FALSE), ==,
                   CS9711_INIT_EPILOGUE_ADVANCE);

  /* Real error, nothing advanced: fail. */
  g_assert_cmpint (cs9711_init_epilogue (TRUE, FALSE, FALSE), ==,
                   CS9711_INIT_EPILOGUE_FAIL);

  /* A read callback will drive the machine: stay, error or not. */
  g_assert_cmpint (cs9711_init_epilogue (FALSE, TRUE, FALSE), ==,
                   CS9711_INIT_EPILOGUE_STAY);
  g_assert_cmpint (cs9711_init_epilogue (TRUE, TRUE, FALSE), ==,
                   CS9711_INIT_EPILOGUE_STAY);

  /* A case body already advanced: stay, and in particular never fail. This is
   * the exact shape of the regression. The error case logs a warning by
   * design (reaching it means a case body leaked a recovered error), so the
   * expected warning is caught rather than escalated by the test harness. */
  g_assert_cmpint (cs9711_init_epilogue (FALSE, FALSE, TRUE), ==,
                   CS9711_INIT_EPILOGUE_STAY);

  g_test_expect_message ("libfprint-cs9711", G_LOG_LEVEL_WARNING,
                         "*error after the init SM advanced*");
  e = cs9711_init_epilogue (TRUE, FALSE, TRUE);
  g_test_assert_expected_messages ();
  g_assert_cmpint (e, ==, CS9711_INIT_EPILOGUE_STAY);
}

static void
test_init_recoverable_error (void)
{
  g_autoptr(GError) timeout_usb = NULL;
  g_autoptr(GError) no_device = NULL;
  g_autoptr(GError) other_domain = NULL;

  timeout_usb = g_error_new_literal (G_USB_DEVICE_ERROR,
                                     G_USB_DEVICE_ERROR_TIMED_OUT, "t");
  no_device = g_error_new_literal (G_USB_DEVICE_ERROR,
                                   G_USB_DEVICE_ERROR_NO_DEVICE, "gone");
  other_domain = g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED, "c");

  /* Only a USB timeout on the init query is the recoverable case; it is the one
   * the state machine clears before advancing. Any other error must fail. */
  g_assert_true (cs9711_init_error_is_recoverable (timeout_usb));
  g_assert_false (cs9711_init_error_is_recoverable (no_device));
  g_assert_false (cs9711_init_error_is_recoverable (other_domain));
  g_assert_false (cs9711_init_error_is_recoverable (NULL));
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
  g_test_add_func ("/cs9711/init-epilogue", test_init_epilogue);
  g_test_add_func ("/cs9711/init-recoverable-error", test_init_recoverable_error);

  return g_test_run ();
}

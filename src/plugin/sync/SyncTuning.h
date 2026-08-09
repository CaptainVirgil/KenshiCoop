#ifndef KENSHICOOP_SYNC_TUNING_H
#define KENSHICOOP_SYNC_TUNING_H

// SyncTuning.h - Phase 6d: the one owned home for the change-gated SAMPLED
// channels' send cadence. Before this, every channel in ReplicatorChannels.cpp
// carried its own function-local SAMPLE_MS / MIN_SEND_MS / RESEND_MS literals;
// they now live here as named fields so the whole channel cadence surface reads
// from a single struct the Replicator owns (SyncTuning tuning_). The channel
// bodies still bind their old local names to these fields, so runtime values are
// byte-identical to the literals they replaced - this is a consolidation, not a
// behavior change. Pure POD (a default ctor sets today's tuned values); no
// mutable file-scope state, C++03/v100-safe, header-only.
//
// SCOPE: only the ChangeGate-policy channels (money + the seven registry
// channels: faction, doors, placed buildings, placed-building doors,
// production, research, deeds). Medical/stats cadence and the drive/combat/interp tunables in
// ReplicatorUtil.h are a different subsystem (Phase 7's drive state machine
// owns those) and intentionally stay where they are.

namespace coop {

struct SyncTuning {
    // Money pool (protocol 52): purchases come in bursts, so a ~1 Hz floor on
    // the host's TOTAL is plenty (a fold publishes immediately regardless - the
    // join is waiting on that answer), and a lost total self-heals on the safety
    // resend. Join DELTAS are never throttled: each one is a conservation event.
    unsigned long moneyMinSendMs;   // sampling floor between pool-total sends
    unsigned long moneyResendMs;    // unchanged-total safety resend

    // Faction relations (protocol 24): relations move in bursts; 1 Hz sample,
    // long safety resend for rows we ever sent.
    unsigned long factionSampleMs;
    unsigned long factionResendMs;

    // Baked doors (protocol 26): doors move in clicks; 1 Hz sample.
    unsigned long doorSampleMs;
    unsigned long doorResendMs;

    // Production machines (protocol 33): machines tick slowly; 1 Hz sample,
    // resend doubles as the join drift corrector.
    unsigned long prodSampleMs;
    unsigned long prodResendMs;

    // Research (protocol 38): unlocks are rare; 1 Hz sample, long lost-row /
    // late-prereq corrector resend.
    unsigned long researchSampleMs;
    unsigned long researchResendMs;

    // Property deeds (protocol 54): purchases are rare; 1 Hz sample. The resend
    // is the lost-row corrector AND the not-yet-resolvable corrector - the peer
    // may have the building's zone unloaded when the row first lands - so it
    // matches the research cadence rather than the shorter door one.
    unsigned long deedSampleMs;
    unsigned long deedResendMs;

    // Placed buildings - construction progress (protocol 27).
    unsigned long buildSampleMs;
    unsigned long buildResendMs;

    // Placed-building doors (protocol 28): mirrors the protocol-26 door cadence.
    unsigned long bdoorSampleMs;
    unsigned long bdoorResendMs;

    // Baked doors, echo hold: after APPLYING a peer's door row, do not publish a
    // contradicting row for that door until this elapses.
    //
    // Kenshi re-closes a door its own logic thinks nobody is holding, so
    // writeDoorByHand reports success and the state does not stick. The old
    // baseline-first echo guard cannot see that: at the next sample the engine
    // reads closed, the baseline says open, and we "correct" the peer - who
    // applies our close, whose engine reopens it, forever. Measured live on
    // 2026-08-07: one bar door, RECV open=1 followed by SEND open=0 ~150 ms
    // later, repeating for the whole session.
    unsigned long doorEchoHoldMs;

    // Mid band (the round-robin that gives far NPCs motion between census
    // beats). midBandMax is how many bodies are eligible; midSliceMax caps the
    // per-tick slice, so cadence is |band|/10 slices = ~2 Hz per body.
    //
    // These were 48 and 16, sized when the mid band was a bandwidth experiment.
    // A shared-cell fight blows straight past that: measured live on 2026-08-07,
    // enum=220 NPCs with 116 authored locally and the band pinned at mid=48, so
    // 68 authored bodies got no motion at all and the join drove them from its
    // own AI until a census beat snapped them (median 128 u, p90 1322 u).
    //
    // The budget was never the constraint. One EntityState is 79 B, so a full
    // 256-body band at ~2 Hz is 256 * 2 * 79 = ~40 KB/s -- nothing for the Steam
    // P2P transport, and the near band already runs 20 Hz alongside it.
    unsigned int  midBandMax;
    unsigned int  midSliceMax;

    // Carry self-heal (protocol 18): how long the stream must KEEP reporting a
    // carry our local copy is not holding before we reproduce it locally.
    //
    // The heal exists only to repair a LOST reliable pickup event, so waiting is
    // free. Firing immediately is not: the drive samples the INTERPOLATED state,
    // which renders up to maxCadenceDelayMs in the past (measured live at 321 ms
    // typical, 802 ms peak), while EVT_DROP_BODY arrives on the reliable channel
    // and applies at once. Without this debounce the stale stream re-lifts a body
    // the peer just set down -- the drop shows up on the other client as a
    // pickup -- and the carryNoSeeTick debounce then drops it ~3 s later, so the
    // pair churns. The drop direction was always debounced; this is its mirror.
    unsigned long carryHealDebounceMs;

    // Inventory snapshot re-assert cadence (protocol 4a). The channel is RELIABLE,
    // so these are not loss recovery - they are a periodic re-assert that also
    // heals a diverged container. A full snapshot is ~10 KB, so the interval is the
    // difference between a censused chest farm costing nothing and costing real
    // bandwidth. Tunable because the right value depends on how much storage is in
    // interest, which is a property of where the players are, not of the code.
    unsigned long invResendMs;      // snapshots below invResendBigN entries
    unsigned long invResendBigMs;   // snapshots at or above it
    unsigned int  invResendBigN;

    // Furniture/chain self-heal (protocol 19/41/42): the same debounce for the same
    // reason. The occupancy heal-ENTER and the shackle relock both read the delayed
    // sample, and both were one-directional -- the EXIT side was debounced by
    // FURN_EXIT_MS while the ENTER side fired on sight. Freeing a caged prisoner
    // therefore read on the peer as an imprisonment, and unshackling one could
    // re-lock permanently.
    unsigned long furnHealDebounceMs;

    // KO-latch release (protocol 54). A reliable EVT_KNOCKOUT pins a body down and
    // only EVT_REVIVE was ever able to unpin it, so a revive that was never sent --
    // or that landed while this client was not receiving -- left the body pinned for
    // the rest of the session. The owner's continuous stream reporting the body
    // upright is the second, always-available witness, and this is how long it has
    // to keep saying so.
    //
    // Longer than the carry/furniture debounces on purpose. Those repair a lost
    // event; this one OVERRIDES a reliable event that did arrive, so the evidence
    // has to be stronger. Do NOT set it to 0: out.bodyState is the interpolated
    // sample and lags real time, so an upright reading can predate the EVT_KNOCKOUT
    // that just landed, and releasing on sight would stand up a body the owner
    // knocked down a moment ago.
    unsigned long koReleaseDebounceMs;

    SyncTuning()
        : moneyMinSendMs(1000),   moneyResendMs(5000),
          factionSampleMs(1000),  factionResendMs(10000),
          doorSampleMs(1000),     doorResendMs(10000),
          prodSampleMs(1000),     prodResendMs(10000),
          researchSampleMs(1000), researchResendMs(15000),
          deedSampleMs(1000),     deedResendMs(15000),
          buildSampleMs(1000),    buildResendMs(10000),
          bdoorSampleMs(1000),    bdoorResendMs(10000),
          doorEchoHoldMs(5000),
          midBandMax(256),        midSliceMax(32),
          carryHealDebounceMs(1500), furnHealDebounceMs(1500),
          koReleaseDebounceMs(3000) {}
};

} // namespace coop

#endif // KENSHICOOP_SYNC_TUNING_H

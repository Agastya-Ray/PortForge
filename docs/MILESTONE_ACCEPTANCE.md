# Milestone acceptance and the next work

The owner's 7 November build specification defines completion. A merge or a
passing instruction subset is progress; a milestone needs all its acceptance
evidence. Work proceeds M1, then M2, then M3. Each gate requires reproducible artifacts from the same source revision.

## M1: translate Windows programs correctly

| Required result | Verified evidence or remaining work |
| --- | --- |
| Integer, x87 and SSE2 instruction classes have at least one million native-reference cases and zero mismatches | Existing native x86 and ARM64 workflow artifacts cover the implemented forms. Full coverage of the selected program corpus is still required; the project does not claim every x86 instruction is implemented. |
| Tier-2 compiler-built programs produce identical output on ARM64 Linux | Eleven programs built with Clang, MinGW and MSVC pass on PR39 commit `9000ddc129e8e6f0072a57997904a13af279eb09`. This unit adds actual x87 numerical program integration; its hosted evidence is pending. |
| Threads and callbacks execute correctly | Still open: real Wine thread entry callbacks, separate CPU/stack/error state, synchronization and memory ordering evidence. |
| Guest exceptions and SEH work in the program corpus | Still open: guest exception records, unwinding and handler callbacks. Returning an arithmetic-fault status does not meet this requirement. SEH means Windows structured exception handling. |
| Floating-point compiler programs execute correctly | This unit adds finite x87 division and control/status transfer. SSE2 floating-point state and operations needed by the corpus remain open. |
| The IR decision is recorded and merged | ADR001 selects PFIR. PR30 merged the correctness/size/timing decision evidence. Remill remains a comparison backend. |

The next substantial implementation units are x87 program integration,
callback-capable imports and real threads, guest exception handling, and the
remaining numerical operations used by the tier-2 corpus. Each unit needs
independent review plus native and ARM64 evidence before it is ready to merge.

## M2: run missed code and learn from it

M2 has not started. After M1 acceptance, embed the runtime translator, write
observed code addresses to `blocks.bin`, and rebuild translated code from that
coverage. Required evidence: runtime-generated code executes correctly, the
coverage loop converges on the whole corpus, and a DLL loaded late through
`LoadLibrary` works. Self-modifying code belongs to this coverage work. A DLL
is a Windows library loaded by an executable.

## M3: prove a real game, without drawing

M3 has not started. Build an open-source Windows game from source, record ten
minutes of play, and replay it headlessly on ARM64 Linux. State hashes must
match and save files must be byte-identical. Compare ahead-of-time translated
execution with fallback-only execution of the same game and replay. If the
translated version is less than 1.3 times as fast, revisit the design before
M4. A synthetic benchmark does not substitute for this game measurement.

Phone graphics, audio and packaging are M4. Opposite-architecture generality
is M5 and AVX hardening is M7; these do not silently expand the M1 gate.

Whole-project completion still requires the explicit owner-approved final
announcement and linked acceptance record requested by the owner. This ledger
does not create that announcement or update the final project guide.

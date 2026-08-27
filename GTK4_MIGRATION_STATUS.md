# GTK4-Migrationsstatus

**Branch:** `feature/gtk4-migration`
**Stand:** 27. August 2026
**Migrationsfortschritt:** **100 % Implementierung**

Diese Branch bleibt bis zur vollständigen Abnahme lokal. Dieser Text ist ein
maintainer-orientierter Nachweisstand, keine Freigabe und kein PR-Text.

## Belegter Umsetzungsstand

- Die aktive Anwendung, ihre Builder-Ressourcen und die umgestellten
  Bedienpfade verwenden GTK4-Modelle, Controller und asynchrone Fortsetzungen.
  Darunter fallen Dateiauswahl, Import und Export, Druck, Register- und
  Reconcile-Abfragen sowie die gemeinsamen Bestätigungs- und
  Fensterlebenszyklen.
- Register- und Reconcile-Fortsetzungen behalten bei asynchronen Antworten
  nur gültige, erneut prüfbare Fachreferenzen. Buch-, Fenster- und
  Transaktionswechsel führen daher nicht zu einer verspäteten Mutation des
  falschen Objekts.
- Der aktive AqBanking-Code verwendet GTK4-Fenster und asynchrone GnuCash-
  Fortsetzungen. Die Gwenhywfar-ABI hat weiterhin synchrone Rückgabewerte;
  die dafür nötigen Adapter sind auf diesen Fremd-ABI-Rand begrenzt und werden
  lokal gegen den tatsächlichen GwenGTK4-Stack gebaut und ausgeführt.
- Der Quellscan findet außerhalb von Tests, Historie und dem bewusst
  inaktiven WebKit1-Altpfad keine aktiven Vorkommen von `GtkTreeView`,
  `GtkTreeModel`, `GtkTreeStore`, `GtkTreeSortable`, `GtkCellRenderer`,
  `GtkAssistant`, `GtkDialog`, `GtkMessageDialog`, `gtk_dialog_run` oder
  `gnc_dialog_run`. Es gibt keinen verbleibenden aktiven
  TreeView-/Assistant-/Dialog-GTK3-Cluster.
- Die CMake-Konfiguration verlangt bei aktivem AqBanking verbindlich
  `gwenhywfar >= 5.14.1`, `aqbanking >= 6.9.1` und für den GnuCash-Build
  `gwengui-gtk4`. Der lokale GwenGTK4-Backendstand ist als
  `fce25f228cfea6ae9ff42fd0e00342fd7eda7aa8` auf `feature/gtk4-gwen` fixiert,
  als GTK4-DLL installiert und über `gwengui-gtk4.pc` verifiziert.
- Der **zentrale sessiongebundene Operationsvertrag mit Lease** (`QofSessionOperationLease`
  für Begin, Load, Save, SafeSave, Export, Scrub, Swap und Destroy) ist
  vollständig implementiert und schützt vor unberechtigten parallelen
  Mutationen, falschen Session-Swaps oder Deadlocks.
- Das XML-Push-Parsing arbeitet reentrant, asynchron und unterbrechbar
  (`qof_session_load_async_with_lease`, `qof_session_cancel_active_load`).
- Datenbereinigungen (Orphan-, Imbalance- und Account-Scrub) laufen als
  kooperative, unterbrechbare Jobs über `gnc-scrub-job-runner` und
  `dialog-lot-viewer` ohne blockierende lokale Hauptschleifen-Pumps.
- Alle **182 von 182 CTest-Testfällen (100 % Pass-Rate)** der gesamten Suite
  wurden nach einer frischen CMake-Konfiguration im MinGW64/MSVCRT-Build
  erfolgreich ausgeführt und bestanden.
- Die VCS-Kennung wird unabhängig vom aufrufenden Buildverzeichnis im
  tatsächlichen Quell-Worktree ermittelt. Ein isolierter Regressionstest
  sichert die saubere Commit-Kennung für externe Buildverzeichnisse ab.
- Die Windows-Paketierung erzeugt aus der ermittelten PE-Importclosure eine
  Inno-Laufzeitliste mit **88 DLLs**. Ein lokaler 64-Bit-Installer wurde damit
  erfolgreich gebaut; die Importclosure ist komplett aufgelöst.
- Der endgültig gestagte Build wurde mit `--nofile` und einem isolierten
  Testprofil gestartet. Das GnuCash-Fenster wurde sichtbar und ließ sich
  regulär mit Exit-Code 0 schließen; Benutzerkonfiguration und echtes Buch
  wurden dabei nicht verwendet.

## Verbleibende Abnahmeschritte

1. **Clean-Machine-E2E.** Den erzeugten Installer auf einem sauberen
   Windows-System installieren und dort Datei öffnen, Speichern und reguläres
   Beenden prüfen.
2. **Finale Freigabe:** Nach diesem externen Installationslauf ist der Branch
   bereit für Push bzw. PR.

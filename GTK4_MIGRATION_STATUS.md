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
  mit dem tatsächlichen GwenGTK4-Stack end-to-end betrieben.
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
- Alle **181 von 181 CTest-Testfällen (100 % Pass-Rate)** der gesamten Suite
  wurden im MinGW64/MSVCRT-Build erfolgreich ausgeführt und bestanden.

## Verbleibende Abnahmeschritte

1. **Paketierung und Clean-Machine-E2E.** Der lokale Buildsupport-Branch
   `feature/gtk4-runtime-bundle` erzeugt die Inno-Laufzeitliste aus der
   tatsächlichen PE-Importclosure. Der Installer wird gebaut und auf
   Vollständigkeit geprüft.
2. **Finale Freigabe:** Nach Installer-Generierung und Validierung ist
   der Branch bereit für Push bzw. PR.
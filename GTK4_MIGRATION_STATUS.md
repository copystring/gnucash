# GTK4-Migrationsstatus

**Branch:** `feature/gtk4-migration`
**Stand:** 13. August 2026
**Migrationsfortschritt:** **98 % Implementierung**

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
  erst mit dem tatsächlichen GwenGTK4-Stack end-to-end abgenommen.
- Der aktuelle Quellscan findet außerhalb von Tests, Historie und dem bewusst
  inaktiven WebKit1-Altpfad keine aktiven Vorkommen von `GtkTreeView`,
  `GtkTreeModel`, `GtkTreeStore`, `GtkTreeSortable`, `GtkCellRenderer`,
  `GtkAssistant`, `GtkDialog`, `GtkMessageDialog`, `gtk_dialog_run` oder
  `gnc_dialog_run`. Es gibt daher keinen verbleibenden aktiven
  TreeView-/Assistant-/Dialog-GTK3-Cluster, der hier als Portierungsarbeit
  ausgewiesen werden müsste.
- Die CMake-Konfiguration verlangt bei aktivem AqBanking verbindlich
  `gwenhywfar >= 5.14.1`, `aqbanking >= 6.9.1` und für den GnuCash-Build
  `gwengui-gtk4`. Der heute vorhandene UCRT-Cache erkennt GTK 4.22.4, ist
  wegen des fehlenden Banking-Stacks jedoch kein vollständiger Configure- oder
  Buildnachweis.

## Verbleibende harte Freigabegates

1. **Fest referenzierter GwenGTK4-Stack.** Der Ziel-Build benötigt die oben
   genannten Gwenhywfar-, AqBanking- und `gwengui-gtk4`-Entwicklungs- und
   Laufzeitkomponenten in einer reproduzierbaren, zum Zieltoolchain passenden
   Form. Solange dieser Stack nicht vorliegt, bleiben Banking-Configure,
   Kompilierung und die PIN-, TAN-, Abbruch- und Fortschrittswege unbelegt.

2. **Vollständige MSVCRT-Abnahme.** Ein frischer Configure und Build,
   Installer-Erzeugung sowie eine echte Windows-End-to-End-Abnahme müssen mit
   dem Ziel-Toolchain- und Laufzeitstack erfolgen. Der vorhandene UCRT-Cache
   mit erkanntem GTK4 ersetzt diese Prüfung nicht.

3. **Zentraler QOF-/Scrub-Fortschrittsvertrag.** Datei laden, speichern und
   exportieren sowie Check-&-Repair/Scrub rufen weiterhin synchrone QOF- und
   Engine-Operationen mit `gnc_window_show_progress` auf. Dieser globale
   Fortschritts-Callback darf nicht durch eine lokale
   `g_main_context_iteration()`-Pumpe kaschiert werden. Er braucht einen
   zentralen, abbrechbaren Ausführungs- und Abschlussvertrag, der Fortschritt
   sicher an die GTK-Hauptschleife übergibt und Buch-, Fenster- und
   Abbruchlebenszyklen absichert.

## Nächste Abnahmefolge

1. Den versionierten GwenGTK4-Stack in der Zielumgebung bereitstellen und den
   vollständigen Banking-Configure durchführen.
2. Den zentralen QOF-/Scrub-Fortschrittsvertrag fertigstellen; danach alle
   betroffenen Datei-, Register-, Reconcile- und Scrub-Aufrufer gegen diesen
   Vertrag prüfen.
3. Den frischen MSVCRT-Build, Installer und die Windows-End-to-End-Abnahme
   ausführen. Erst danach ist ein Push oder PR fachlich vertretbar.

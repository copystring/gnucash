# GTK4-Migrationsstatus

Dieser Status gilt für `feature/gtk4-migration`. Die Branch ist eine
Integrationsbranch für einen großen PR nach `future`; sie ist ausdrücklich
nicht freigabefähig und darf bis zur vollständigen Abnahme nicht gepusht oder
als PR eröffnet werden.

## In dieser Branch bereits umgesetzt

- Die Anwendung verwendet `GtkApplication` statt eines globalen GTK-Hauptloops.
- Der HTML-Controller `GncHtml` ist von `GtkBin` entkoppelt; Backends liefern
  ihr sichtbares Widget direkt.
- Die aktiven CMake-Pfade verlangen GTK 4.14 und WebKitGTK 6 auf Linux. Der
  Windows-Pfad verwendet WebView2; der macOS-Pfad verwendet jetzt WKWebView.
- Die 91 Builder-Ressourcen unter `gnucash/` wurden mit
  `gtk4-builder-tool validate` geprüft. Die dabei gefundenen veralteten
  Eigenschaften und Signale wurden entfernt oder durch Controller ersetzt.
- Kontensuche, Import-Map-Editor und IMAP-Editor verwenden GTK4-Modelle und
  `GtkColumnView`; Auswahl, Gruppenlöschung und die wichtigen Dialogdetails
  wurden dabei wiederhergestellt.
- Die Berichtoptionen verwenden für einfache Auswahlwerte, Listen, Budgets und
  Kontenlisten GTK4-Modelle. Die Kontenliste hält ihre Auswahl über GUIDs,
  sodass Typ-/Hidden-Filter und Reloads des gemeinsamen Kontenmodells keine
  Auswahl auf eine andere Zeile verschieben. Bilddatei-Optionen verwenden
  `GtkFileDialog` mit Vorschau und Clear-Operation.
- Mehrere Fenster- und Dialogpfade verwenden nun `close-request` sowie
  `GtkEventControllerKey`/`GtkEventControllerFocus` statt direkter GTK3-
  Ereignissignale.
- Die gemeinsame Dateiauswahl verwendet `GtkFileDialog` mit asynchronem
  Request-/Finish-Vertrag. Dateiöffnung, Speichern vor dem Schließen und der
  XML-Encoding-Assistent setzen diese Fortsetzung ohne Ersatz-Hauptschleife
  ein; Erkennung und Konvertierung behandeln XML-v2 mit und ohne deklarierte
  Zeichenkodierung sowie gzip-Dateien.
- Preferences, Commodity-Manager, Stock-Split-Assistent, mehrere AqBanking-
  Transfers und die wichtigsten Importpfade sind auf GTK4-Modelle beziehungsweise
  asynchrone Fenster umgestellt. Ihre gezielten Builder-, Syntax- und
  Vertragsprüfungen sind Teil der lokalen Nachweise, aber kein Ersatz für die
  plattformübergreifende Gesamtabnahme.
- Der WKWebView-Backendpfad hält eine verwaltete native Unteransicht synchron
  mit der GTK-Allokation, begrenzt den Dateizugriff auf den temporären
  Reportbereich und blockiert jede Navigation, die nicht vom gemeinsamen
  internen URL-Vertrag akzeptiert wird.

## Noch offen – Freigabeblocker

### Datenansichten und Register

- Das Register ist weiterhin ein zusammenhängender GTK3-Ereignisverbund aus
  `GnucashSheet`, `GncItemEdit`, `GncItemList`, Popup-Zellen und dem
  tabellenneutralen Ledger-Kern. Die vorhandenen `GdkEvent`-Weitergaben,
  `GtkLayout`-Flächen und Offscreen-Annahmen müssen durch einen gemeinsamen
  GTK4-Controller ersetzt werden; eine isolierte Portierung einzelner Zellen
  wäre Funktionsverlust.
- Die Terminbuchungsseite besteht aus der Seite selbst,
  `GncTreeViewSxList` und `GncSxListTreeModelAdapter`. Letzterer implementiert
  noch `GtkTreeModel`/`GtkTreeSortable` auf `GtkTreeStore` und
  `GtkTreeModelSort`. Erst ein gemeinsames GObject-Zeilenmodell mit
  `GListModel`, Sortern, Mehrfachauswahl, Kontextmenü, Leertasten-Umschalten
  und Wiederherstellung der Auswahl schließt diesen Pfad.
- Weitere aktive `GtkTreeView`-/CellRenderer-Pfade müssen jeweils mitsamt
  Sortierung, Filterung, Bearbeitung, Drag-and-drop, Tooltips, Aktionen und
  Accessibility nach `GtkColumnView`/`GtkListView` portiert werden.

### Reports und Plattformen

- Die gemeinsame `GncHtml`-Schnittstelle braucht noch Suche, view-lokalen
  Zoom und einen expliziten PDF-Exportvertrag. Diese Funktionen müssen in
  WebKitGTK 6, WebView2 und WKWebView gleich behandelt und getestet werden.
- Der WKWebView-Code benötigt einen echten macOS-Configure/Build/Test gegen
  `gtk4-macos`; Windows benötigt Configure/Build/Installer-Abnahme mit
  WebView2Loader und Evergreen Runtime. Beide Plattformen sind ohne diese
  Matrix nicht freigegeben.
- Linux-Berichte benötigen automatisierte Tests für Navigation,
  Ladefehler, Abbruch, Druck und PDF. Der temporäre Reportdateizugriff muss
  mit seinem begrenzten Read-Root als Sicherheitsvertrag dokumentiert und
  getestet werden.

### Banking, Import und Export

- AqBanking benötigt einen nachweislich geprüften, fest referenzierten
  `gwengui-gtk4`-Stand. Die synchronen Gwen-Dialoge, die lokale
  Main-Context-Pumpe sowie TAN-, Flicker-, PIN-, Fortschritts- und
  Abbruchpfade müssen auf den asynchronen GTK4-Lebenszyklus überführt und
  end-to-end geprüft werden.
- OFX bleibt aktiviert, benötigt jedoch Import-Fixtures und End-to-End-Tests;
  ein bloßer Linktest reicht nicht als Funktionsnachweis.

### Gesamtabnahme

- Große, noch nicht portierte GTK3-Cluster sind die Assistenten, Commodity-
  Auswahl und -Bearbeitung, die allgemeinen Query-/Bestätigungsdienste sowie
  weitere Register- und Buchungsaktionen. Jeder davon braucht einen echten
  asynchronen Aufrufervertrag; eine lokale `GtkDialog::response`-Umstellung
  ohne die Fortsetzung des fachlichen Workflows wäre unvollständig.
- Alle verbliebenen GTK3-/GDK3-APIs, direkten Ereignisstrukturen,
  blockierenden Dialogaufrufe und aktiven Paketierungsreferenzen müssen
  vollständig erfasst und entfernt werden. Der Scan umfasst weiterhin unter
  anderem TreeView-/CellRenderer-Pfade, `GtkAssistant`, Register-Layout und
  Dialog-Wrapper. Inaktive Altcodedateien werden erst nach Ersetzung ihres
  Laufzeitpfads entfernt.
- Appearance-Service, Hell-/Dunkel-/Systemmodus, High-DPI, mehrere Monitore
  und native Titelleisten benötigen eine plattformübergreifende Abnahme.
- CI und Paketierung brauchen eine grüne Linux-, Windows- und macOS-Matrix
  inklusive Builder-Validierung, Unit-Tests, E2E für Dateiöffnung durch eine
  zweite Instanz, Register, Dialoge, Reports, Drucken/PDF, Banking, OFX,
  Import/Export, Zwischenablage, Drag-and-drop und Accessibility.
- Der lokale Ordner `build-gtk4-migration/` bleibt unversioniert und darf
  nicht Teil des PRs werden.

## Lokale Validierungsgrenze

Auf dem aktuellen Windows-Arbeitsplatz fehlt Guile, daher kann die vollständige
CMake-Konfiguration nicht bis zum Build durchlaufen. Diese Umgebung darf
keinen grünen Gesamtbuild vortäuschen: alle Plattform- und E2E-Kriterien oben
bleiben harte Freigabeblocker, bis sie in der jeweiligen Zielumgebung belegt
sind.

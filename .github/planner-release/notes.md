Prima release di **CrossInk-Planner**: CrossInk v1.6.1 per Xteink X3/X4 con To-Do List, Calendario e app companion.

### File
- `firmware-x3-x4.bin` — firmware per **Xteink X3 e X4**. Installalo da **Impostazioni → Sistema → Aggiornamento firmware da SD** o con esptool (vedi il [README](https://github.com/lorenzocannavacciuolo12500-dot/CrossInk-Planner#installazione)).
- `crossink-companion-v0.3.1.apk` — app companion per Android 6+ (stessa firma di tutte le versioni dalla 0.2.1).

### Novità rispetto a CrossInk v1.6.1
- To-Do List sul lettore: spunta, aggiungi con la tastiera, tieni premuto Conferma per eliminare, elimina le completate. Salvata sulla SD.
- Calendario in sola lettura, copiato dall'app dagli indirizzi iCal privati (Google Calendar, Outlook, iCloud).
- Tasti laterali: Calendario ← Home → To-Do List, con i puntini di pagina nella barra di stato.
- Voce "Agenda" nella Home e schermata di sospensione "Agenda".
- Wi-Fi in background su To-Do List e Calendario, così l'app sincronizza da sola; al risveglio il lettore riapre la pagina su cui era.
- Data, ora e fuso orario impostati dall'app a ogni sincronizzazione.

### App companion v0.3.1
To-Do, Calendario (iCal), invio libri e siti di e-book, temi Scuro/Nero/Chiaro, scanner QR, sincronizzazione automatica ogni 45 secondi.

Non ancora provato su un lettore vero: compilato e verificato su GitHub Actions (test nativi, clang-format, cppcheck, build X3/X4 e X4 Pro) e nel simulatore.

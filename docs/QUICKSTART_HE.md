# ApkVulFuzz — העדכון שלי + מדריך לחבר צוות

היקף: רק העבודה שנעשתה מאז הפגישה האחרונה, ואיך חבר צוות חדש מפעיל ובודק את זה.
(הרקע על הפרויקט הכללי הוא של המנחה — לא חוזרים עליו כאן.)

---

## מה עשיתי מאז הפגישה האחרונה (12 שלבים)

**אימות ה-workflow הקיים של AFL**
1. וידאתי שהוא **נבנה ורץ מקצה-לקצה** — מתקמפל מול AFL++, מייצא את פונקציות
   ה-`afl_custom_*`, וריצת `afl-fuzz` אמיתית מייצרת `fuzzer_stats` בלי קריסה.
2. וידאתי ש-AFL רץ עם **ה-custom mutator שלנו בלבד** (`AFL_CUSTOM_MUTATOR_ONLY=1`
   מכבה את אסטרטגיות המוטציה המובנות של AFL — havoc/deterministic).
3. חקרתי את **התנהגות ריבוי ה-APK** וגיליתי (בריצות אמיתיות) ש-AFL **"מתנעל" על
   APK אחד** אלא אם ה-coverage שונה לכל APK — הפתרון: instance נפרד של AFL לכל APK.

**כלי corpus + הרצה/ניתוח (סקריפטים חדשים)**
4. `validate-corpus.py` — מאמת כל APK (נפתח, יש מניפסט שאפשר לאתר) + מחולל מחדש את קבצי ה-path; מוכן להתרחבות ל-~20 APKs.
5. `run-corpus-smoke.sh` — פקודה אחת ל-smoke test על כל ה-corpus (ונכשל בקול אם AFL קורס).
6. `triage-afl-output.py` — מסכם ריצת AFL: crashes, hangs, coverage, ואילו seeds שומשו.
7. `test-apk-tools.py` — מריץ APKs מוטנטיים דרך `unzip`/`zipinfo`/`7z`/Python `zipfile`/`file`/`aapt` ומסווג כל תוצאה.
8. ...עם **זיהוי crash / segfault / hang של כלים** ושמירה אוטומטית של כל דגימה שמפילה כלי.

**Mutator רב-אסטרטגי חדש (V2)**
9. **אזור מוטציה שניתן להגדרה** — offset, גודל segment, ומספר ה-bit-flips.
10. **פיצול המוטציה לקבצי אסטרטגיה נפרדים** (mutator חדש, נפרד מה-baseline).
11. **בחירת אסטרטגיה בזמן ריצה** — `APKFUZZ_MUTATOR_MODE` בוחר אחת מ-**10** אסטרטגיות.
12. **שיטת השוואת mutators** + `compare-mutators.py` כדי לדרג איזו אסטרטגיה הכי טובה.

> נותר לעשות (שלב 6 ב-roadmap): הקמפיין הארוך של ~12 שעות — נדחה, אריץ אותו בהמשך.

---

## V1 מול V2 (שני ה-mutators)

שניהם custom mutators של AFL++ שמאתרים את `AndroidManifest.xml` דינמית והופכים
ביטים בתוכו. ההבדל:

| | **V1** (`cm-ApkVulFuzz.so`) | **V2** (`cm-ApkVulFuzz2.so`) |
|---|---|---|
| התנהגות | מצב קבוע אחד: הופך 1–50 ביטים אקראיים על כל המניפסט | אותו דבר כברירת מחדל, **+ 9 אסטרטגיות נוספות** שנבחרות בזמן ריצה |
| בחירת אזור היעד | לא | כן (חלקי מניפסט, ZIP header, central directory, שדות CRC/גודל, אקראי) |
| עוצמה / logging / שמירת מוטנטים | לא | כן (`APKFUZZ_MAX_FLIPS`, `APKFUZZ_LOG`, `APKFUZZ_SAVE_DIR`) |

**השתמש ב-V1** כ-baseline פשוט; **השתמש ב-V2** לניסויים ולהשוואות. בלי משתני סביבה,
V2 מתנהג כמו V1. בוחרים את ה-mutator עם `AFL_CUSTOM_MUTATOR_LIBRARY`, ואת אסטרטגיית
ה-V2 עם `APKFUZZ_MUTATOR_MODE`.

---

## איך מפעילים

מתיקיית השורש של הפרויקט, במכונה עם AFL++ מותקן:
```bash
# 1. בניית שני ה-mutators
cd src
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutators.sh       # V1 → build/cm-ApkVulFuzz.so
AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh     # V2 → build/cm-ApkVulFuzz2.so
cd ..

# 2. אימות ורענון קבצי ה-path של ה-corpus
python3 scripts/validate-corpus.py --write-inputs

# 3. הרצת fuzzing על כל ה-corpus:  <שניות> <mutator.so> <אסטרטגיה>
bash scripts/run-corpus-smoke.sh 30 build/cm-ApkVulFuzz2.so manifest-bitflip

# 4. (אופציונלי) שמירת מוטנטים וניתוח
export APKFUZZ_SAVE_DIR=mutants-run1
bash scripts/run-corpus-smoke.sh 300 build/cm-ApkVulFuzz2.so crc-fields
python3 scripts/triage-afl-output.py afl-corpus-smoke-*/
python3 scripts/test-apk-tools.py mutants-run1 --preserve crashers
```
אסטרטגיות V2: `manifest-bitflip`, `manifest-segment`, `manifest-start`,
`manifest-middle`, `manifest-end`, `zip-local-header`, `zip-central-directory`,
`crc-fields`, `size-fields`, `random-range`.

---

## איך בודקים (תן את זה לחבר הצוות שלך)

בערך 2 דקות כדי לוודא שזה עובד במכונה שלך:
```bash
# A. ה-mutator נבנה ומייצא את ה-hooks של AFL
cd src && AFLPP_DIR=~/AFLplusplus bash compile_afl_mutator_v2.sh && cd ..
nm -D build/cm-ApkVulFuzz2.so | grep afl_custom
#    מצופה: afl_custom_init, afl_custom_fuzz, afl_custom_deinit

# B. ה-corpus תקין
python3 scripts/validate-corpus.py
#    מצופה: כל APK מסומן "USABLE" עם manifest_range

# C. smoke של שנייה אחת שמריץ את AFL עם ה-mutator שלנו
bash scripts/run-corpus-smoke.sh 1 build/cm-ApkVulFuzz2.so manifest-bitflip
#    מצופה: מודפס fuzzer_stats (execs_done > 0); AFL מודיע שה-custom mutator נטען;
#            ולא מודפס "Smoke test FAILED"
```
זה שבור אם: `nm` לא מראה אף סמל `afl_custom_*` (בנייה כושלת); `validate-corpus.py`
מסמן APK כ-`BAD` (ZIP פגום / אין מניפסט); או שה-smoke מדפיס **`Smoke test FAILED`**
/ אין `fuzzer_stats` (AFL קרס — בדרך כלל mutator חסר/פגום, seeds לא תקינים, או harness שבור).

בדיקת שפיות של מוטציה בודדת בלי AFL:
```bash
APKFUZZ_MUTATOR_MODE=manifest-bitflip build/mut2-standalone Evaluation-SSBSE-2026/apk/F-Droid.apk
#    מצופה: מודפס הנתיב של קובץ APK מוטנטי חדש שנוצר
```

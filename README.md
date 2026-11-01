# schedule_search

מנוע שמוצא את הסידור הטוב ביותר למשמרות לפי דרישות המשתמש, באמצעות חיפוש לעומק (DFS) עם backtracking ותנאים.

- `PLAN.md`: שלבי העבודה והסטטוס של כל אחד
- `DECISIONS.md`: ההחלטות והנימוקים שלהן

## בנייה

**Visual Studio 2022:** File → Open → Folder, ובוחרים את התיקייה הזו. VS מזהה את `CMakeLists.txt` לבד.

**שורת פקודה:**
```
cmake -S . -B build
cmake --build build
```

## הרצה
```
schedule_search data/example_week.json
```

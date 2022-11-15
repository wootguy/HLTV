cd "C:\Games\Steam\steamapps\common\Sven Co-op\svencoop\addons\metamod\dlls"

if exist SvenTV_old.dll (
    del SvenTV_old.dll
)
if exist SvenTV.dll (
    rename SvenTV.dll SvenTV_old.dll 
)

exit /b 0
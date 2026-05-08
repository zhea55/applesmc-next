savedcmd_applesmc/applesmc.mod := printf '%s\n'   applesmc.o | awk '!x[$$0]++ { print("applesmc/"$$0) }' > applesmc/applesmc.mod

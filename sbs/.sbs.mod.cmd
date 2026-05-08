savedcmd_sbs/sbs.mod := printf '%s\n'   sbs.o | awk '!x[$$0]++ { print("sbs/"$$0) }' > sbs/sbs.mod

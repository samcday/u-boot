ifeq ($(TYPE),prod)
TEXT_BASE = 0x10005000
else
TEXT_BASE = 0x87F00000
endif

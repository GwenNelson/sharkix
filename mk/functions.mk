define require-variable
$(if $(strip $($1)),,$(error required variable '$1' is not set))
endef

define require-file
$(if $(wildcard $1),,$(error required build file '$1' does not exist))
endef

INCLUDED_SELECTIONS :=
define include-selection
$(if $(filter $1,$(INCLUDED_SELECTIONS)),,$(eval INCLUDED_SELECTIONS += $1)$(call require-file,$1)$(eval include $1))
endef

INCLUDED_MODULES :=
define include-module
$(if $(filter $1,$(INCLUDED_MODULES)),,$(eval INCLUDED_MODULES += $1)$(call require-file,$1/module.mk)$(eval include $1/module.mk))
endef

$(info Reading ex-contents.mk $(COMPONENT_DIR))

VPATH+=$(COMPONENT_DIR)/testm

OFLUX_PROJECT_NAME:=module_self


OFLUX_PROJECTS+= $(OFLUX_PROJECT_NAME)
OFLUX_TESTS+= run-$(OFLUX_PROJECT_NAME).sh

$(OFLUX_PROJECT_NAME)_OFLUX_MODULES:= testm_self.flux

$(OFLUX_PROJECT_NAME)_OFLUX_MAIN:=$(OFLUX_PROJECT_NAME).flux

$(OFLUX_PROJECT_NAME)_OFLUX_SRC:= \
	mImpl_$(OFLUX_PROJECT_NAME).cpp \
	mImpl_$(OFLUX_PROJECT_NAME)_testm.cpp

$(OFLUX_PROJECT_NAME)_OFLUX_CXXFLAGS:= -DTESTING


$(OFLUX_PROJECT_NAME)_OFLUX_PATH:=$(COMPONENT_DIR)
$(OFLUX_PROJECT_NAME)_OFLUX_INCS:= \
	-I $($(OFLUX_PROJECT_NAME)_OFLUX_PATH) \
	-I $(COMPONENT_DIR)/testm

$(OFLUX_PROJECT_NAME)_OFLUX_INCLUDES:= 



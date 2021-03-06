$(info Make v:$(MAKE_VERSION) Starting oflux main makefile, goal:$(MAKECMDGOALS))

ifeq (,$(filter _%,$(notdir $(CURDIR))))
include Mk/target.mk
else
#----- Begin Boilerplate

RELEASE ?= production
VPATH := $(SRCDIR)
OFLUX_PROJECTS :=
OFLUX_PLUGIN_PROJECTS :=
OFLUX_TESTS :=
OFLUX_UNIT_TESTS :=
OFLUX_DOCUMENTATION:=
 
include $(SRCDIR)/Mk/rules.mk


all: build

doc/examples:
	mkdir -p doc/examples

#begin debug helpers

print-%: ; @echo $* is $($*)

OLD_SHELL := $(SHELL)
	SHELL = $(if $@,$(info [$@ ($^)($?)])$(OLD_SHELL),$(OLD_SHELL))

#end debug helpers

DIR_SPECIFIC_MAKEFILE_NAME := contents.mk
EX_DIR_SPECIFIC_MAKEFILE_NAME := ex-contents.mk

buildable_dirs = \
	$(shell find $(SRCDIR) -name $(DIR_SPECIFIC_MAKEFILE_NAME) | \
	sed 's/$(DIR_SPECIFIC_MAKEFILE_NAME)//' | sort)
ex_buildable_dirs = \
	$(shell find $(SRCDIR) -name $(EX_DIR_SPECIFIC_MAKEFILE_NAME) | \
	sed 's/$(EX_DIR_SPECIFIC_MAKEFILE_NAME)//' | sort) \


define process_dir
  LIBRARIES :=

  # passed to the contents.mk module files
  COMPONENT_DIR := $1
  include $1/$(2)contents.mk
  ALL_TOOLS += $$(TOOLS)
  ALL_LIBRARIES += $$(LIBRARIES)
  ALL_APPS += $$(APPS)
  VPATH += $1
  #
  # The last line of the function must be left blank
  # in order to avoid some quirky, broken gmake
  # behavior when expanding macros within foreach
  # loops.
  #

endef


define process_dirs
  $(foreach DIR, $(buildable_dirs),\
	$(call process_dir,$(DIR)))
  $(foreach DIR, $(ex_buildable_dirs),\
	$(call process_dir,$(DIR),ex-))
endef

OFLUXRTLIBS += -lofshim

define process_oflux_project
$(1)_OFLUX_MODULE_OBJS:=$$($(1)_OFLUX_MODULES:%.flux=OFluxGenerate_$(1)_%.o)
$(1)_OFLUX_MAIN_TARGET_OBJ:=OFluxGenerate_$(1).o
$(1)_OFLUX_OBJS:=$$($(1)_OFLUX_MAIN_TARGET_OBJ) $$($(1)_OFLUX_MODULE_OBJS) $$($(1)_OFLUX_SRC:%.cpp=%.o) 
$(1)_OFLUX_SO_OBJS:= $$($(1)_OFLUX_OBJS:.o=.pic.o)
$(1)_OFLUX_SO_TARGET:=$$($(1)_OFLUX_KERNEL:%.cpp=lib%.so)
$(1)_OFLUX_KERNEL_DIR:=$$($(1)_OFLUX_KERNEL:%.cpp=%dir)
$(1)_OFLUX_MODULE_CPPS:=$$($(1)_OFLUX_MODULES:%.flux=OFluxGenerate_$(1)_%.cpp)
$(1)_OFLUX_OPTS+=$$(if $$($(1)_OFLUX_KERNEL),-absterm,) -duribase ''
$(1)_OFLUX_MAIN_OBJ_DEP:=$$(if $$($(1)_OFLUX_KERNEL),$$($(1)_OFLUX_KERNEL:%.cpp=%.o),$$($(1)_OFLUX_OBJS))
$(1)_HAS_PLUGINS:=$$(foreach P,$(OFLUX_PLUGIN_PROJECTS),$$(if $$(filter $(1),$$($$(P)_FROM_PROJECT)), $$(P),))
$(1)_OFLUX_MAIN_TARGET:=$$($(1)_OFLUX_MAIN:%.flux=%)
$(1)_RUN_SCRIPT:=$$($(1)_OFLUX_MAIN:%.flux=run-%.sh)
$(1)_OFLUX_SVG:= $$(foreach S,$$($(1)_OFLUX_MAIN:.flux=.svg) $$($(1)_OFLUX_MAIN:.flux=-flat.svg) $$($(1)_OFLUX_MODULES:.flux=.svg),doc/examples/$$(S))
$(1)_OFLUX_HTML:= $$(foreach S,$$($(1)_OFLUX_MAIN:.flux=.html) $$($(1)_OFLUX_MODULES:.flux=.html),doc/examples/$$(S))

OFluxGenerate_$(1)_%.h OFluxGenerate_$(1)_%.cpp : %.flux mImpl_%.h oflux 
	$(OFLUXCOMPILER) $$($(1)_OFLUX_OPTS) -oprefix OFluxGenerate_$(1) -a $$* $$($(1)_OFLUX_INCS) $$<

OFluxGenerate_$(1).h  : $$($(1)_OFLUX_MAIN) $$($(1)_OFLUX_PATH)/mImpl.h $$($(1)_OFLUX_MODULE_CPPS) oflux 
	$(OFLUXCOMPILER) $$($(1)_OFLUX_OPTS) -oprefix OFluxGenerate_$(1) $$($(1)_OFLUX_INCS) $$($(1)_OFLUX_MAIN)

OFluxGenerate_$(1).cpp : OFluxGenerate_$(1).h

$$($(1)_OFLUX_KERNEL_DIR) : 
	mkdir -p $$@/xml; \
	mkdir -p $$@/lib

$$($(1)_OFLUX_SO_TARGET) : $$($(1)_OFLUX_SO_OBJS) liboflux.so
	$(CXX) -shared $$^ $(OFLUXRTLIBS) -o $$@

$$($(1)_OFLUX_MAIN_TARGET) : $$($(1)_OFLUX_MAIN_OBJ_DEP) $$($(1)_OFLUX_SO_TARGET) liboflux.so libofshim.so  
	$(CXX) $(CXXOPTS) $$($(1)_OFLUX_CXXFLAGS) $(INCS) $(LIBDIRS) $$($(1)_OFLUX_MAIN_OBJ_DEP) $$($(1)_OFLUX_KERNEL:%.cpp=-l%) liboflux.so libofshim.so $(LIBS) -o $$@

$$($(1)_RUN_SCRIPT) : $$($(1)_OFLUX_MAIN_TARGET) $$($(1)_OFLUX_KERNEL_DIR) libofshim.so
	echo "#!$(shell which bash)" > $$@; \
	echo "" >> $$@; \
	echo "export LD_LIBRARY_PATH=$(shell pwd):$$$$LD_LIBRARY_PATH" >> $$@; \
	echo "" >> $$@; \
	echo $(EXTRA_RUN_ENVIRONMENT) >> $$@; \
	$$(if $$($(1)_OFLUX_KERNEL),echo "export LD_LIBRARY_PATH=$(shell pwd):$$$$LD_LIBRARY_PATH" >> $$@; \
	echo "pushd .; cd $(shell pwd)/$$($(1)_OFLUX_KERNEL_DIR)" >> $$@;,) \
	(echo $(shell pwd)/$$($(1)_OFLUX_MAIN:%.flux=%) " " $(shell pwd)/$$($(1)_OFLUX_MAIN:%.flux=%.xml) "@: @," | sed -e 's/:/1/g' | sed -e 's/\,/2/g' | sed -e 's/@/$$$$/g') >> $$@; \
	chmod +x $$@

$(1)_load_test: $$($(1)_RUN_SCRIPT)
	OFLUX_CONFIG=nostart $(CURDIR)/$$($(1)_RUN_SCRIPT) $$($(1)_LOADTEST_ARGS) > $$@

$(1)_verify_load: $(1)_load_test $$($(1)_OFLUX_GEN_HEADERS) $$(foreach P,$$($(1)_HAS_PLUGINS), $$($$(P)_OFLUX_GEN_HEADERS) $$(P)_done)
	((cat $$(filter %.h,$$?) | sed 's/^/C /g'); sed 's/^/L /g' $(1)_load_test) | awk -f $(SRCDIR)/tests/build/verify_io_conv.awk

$$($(1)_OFLUX_MAIN:.flux=-flat.dot) $$($(1)_OFLUX_MAIN:.flux=.dot) : OFluxGenerate_$(1).h
$$($(1)_OFLUX_MODULES:.flux=.dot) : %.dot : OFluxGenerate_$(1)_%.h

$$($(1)_OFLUX_SVG) : doc/examples/%.svg : %.dot doc/examples
	$(if $(DOT),$(DOT),$(warning has no dot); echo dot) $$^ > $$@

$$($(1)_OFLUX_HTML) : doc/examples/%.html : %.flux doc/examples
	$(OFLUX_DOC_COMPONENT_DIR)/oflux-doc.sh $$^ > $$@

OFLUX_DOCUMENTATION += $$($(1)_OFLUX_SVG) $$($(1)_OFLUX_HTML)

$$($(1)_OFLUX_KERNEL:%.cpp=%.o) $$($(1)_OFLUX_OBJS) $$($(1)_OFLUX_SO_OBJS): INCS = $(INCS) $$($(1)_OFLUX_CXXFLAGS) \
	$$($(1)_OFLUX_INCS)
$$($(1)_OFLUX_KERNEL:%.cpp=%.o) $$($(1)_OFLUX_OBJS) $$($(1)_OFLUX_SO_OBJS): OFluxGenerate_$(1).cpp
$$($(1)_OFLUX_SRC): $$($(1)_OFLUX_MODULE_OBJS:%.o=%.h) $$($(1)_OFLUX_MAIN_TARGET_OBJ:%.o=%.h)
#
# no link rule done

endef


define process_oflux_plugin_project
$(1)_OFLUX_MODULE_OBJS:=$$($(1)_OFLUX_MODULES:%.flux=OFluxGenerate_$$($(1)_FROM_PROJECT)_%.pic.o)
$(1)_OFLUX_MAIN_TARGET_OBJ:=OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).pic.o
$(1)_OFLUX_SO_OBJS:=$$($(1)_OFLUX_MAIN_TARGET_OBJ) $$($(1)_OFLUX_MODULE_OBJS) $$($(1)_OFLUX_SRC:%.cpp=%.pic.o)
$(1)_OFLUX_SO_TARGET:=lib$(1).so
$(1)_OFLUX_SO_KERNEL:=lib$$($(1)_FROM_PROJECT).so
$(1)_OFLUX_SO_DEPS:=$$($(1)_OFLUX_SO_KERNEL) $$($(1)_DEP_PLUGINS:%=lib%.so)
$(1)_OFLUX_KERNEL_DIR:=$$($$($(1)_FROM_PROJECT)_OFLUX_KERNEL:%.cpp=%dir)
$(1)_OFLUX_MODULE_CPPS:=$$($(1)_OFLUX_MODULES:%.flux=OFluxGenerate_$$($(1)_FROM_PROJECT)_%.cpp)
$(1)_OFLUX_INCS+=$$($$($(1)_FROM_PROJECT)_OFLUX_PATH:%=-I %) \
	$$(foreach P,$$($(1)_DEP_PLUGINS),$$($$(P)_OFLUX_PATH:%=-I %))
$(1)_OFLUX_SVG:=$$(foreach S,$$($(1)_OFLUX_MAIN:.flux=.svg) $$($(1)_OFLUX_MODULES:.flux=.svg),doc/examples/$$(S))
$(1)_OFLUX_HTML:=$$(foreach S,$$($(1)_OFLUX_MAIN:.flux=.html) $$($(1)_OFLUX_MODULES:.flux=.html),doc/examples/$$(S))
$(1)_OFLUX_OPTS+=-duribase ''

$(1)_OFLUX_FINAL:=$$($(1)_OFLUX_KERNEL_DIR)/lib/$$($(1)_OFLUX_SO_TARGET)

OFluxGenerate_$$($(1)_FROM_PROJECT)_%.h : %.flux mImpl_%.h oflux 
	$(OFLUXCOMPILER) $$($(1)_OFLUX_OPTS) -oprefix OFluxGenerate_$$($(1)_FROM_PROJECT) -a $$* $$($(1)_OFLUX_INCS) $$<

OFluxGenerate_$$($(1)_FROM_PROJECT)_%.cpp : OFluxGenerate_$$($(1)_FROM_PROJECT)_%.h

OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).h OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).cpp : $$($(1)_OFLUX_MAIN) mImpl_$(1).h $$($(1)_OFLUX_MODULE_CPPS) oflux $$($(1)_OFLUX_KERNEL_DIR) 
	$(OFLUXCOMPILER) $$($(1)_OFLUX_OPTS) -oprefix OFluxGenerate_$$($(1)_FROM_PROJECT) -p $(1) $$($(1)_OFLUX_INCS) $$($(1)_OFLUX_MAIN)
	rm -f $$($(1)_OFLUX_KERNEL_DIR)/xml/$(1).xml
	ln -sf $(CURDIR)/$(1).xml $$($(1)_OFLUX_KERNEL_DIR)/xml/$(1).xml

OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).cpp : OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).h

$$($(1)_OFLUX_SO_TARGET) : $$($(1)_OFLUX_SO_OBJS) $$($(1)_OFLUX_SO_DEPS) liboflux.so
	$(CXX) -shared -L. $$^ $(OFLUXRTLIBS) -o $$@

$$($(1)_OFLUX_FINAL) : $$($(1)_OFLUX_KERNEL_DIR) $$($(1)_OFLUX_SO_TARGET)
	ln -sf $(CURDIR)/$$($(1)_OFLUX_SO_TARGET) $$($(1)_OFLUX_FINAL)

$(1)_done : $$($(1)_OFLUX_FINAL)
	touch $(1)_done

$$($(1)_OFLUX_MODULES:.flux=.dot) : %.dot : OFluxGenerate_$$($(1)_FROM_PROJECT)_%.h
$$($(1)_OFLUX_MAIN:.flux=.dot) : OFluxGenerate_$$($(1)_FROM_PROJECT)_$(1).h
$$($(1)_OFLUX_SVG) : doc/examples/%.svg : %.dot doc/examples
	$(if $(DOT),$(DOT),$(warning has no dot); echo dot) $$^ > $$@

$$($(1)_OFLUX_HTML) : doc/examples/%.html : %.flux doc/examples
	$(OFLUX_DOC_COMPONENT_DIR)/oflux-doc.sh $$^ > $$@

OFLUX_DOCUMENTATION += \
  $$($(1)_OFLUX_SVG) $$($(1)_OFLUX_HTML)

$$($(1)_OFLUX_SO_OBJS) : INCS = $(INCS) $$($(1)_OFLUX_CXXFLAGS) \
	$$($(1)_OFLUX_INCS)
$$($(1)_OFLUX_SO_OBJS): OFluxGenerate_$$($(1)_FROM_PROJECT).cpp $$($(1)_DEP_PLUGINS:%=OFluxGenerate_$$($(1)_FROM_PROJECT)_%.cpp)
$$($(1)_OFLUX_SRC): $$($(1)_OFLUX_MODULE_OBJS:%.pic.o=%.h) $$($(1)_OFLUX_MAIN_TARGET_OBJ:%.pic.o=%.h) 
#
# no link rule done

endef

define process_oflux_projects
  $(foreach PROJ,$(OFLUX_PROJECTS),\
	$(call process_oflux_project,$(notdir $(PROJ)),$(dir $(PROJ)))) \
  $(foreach PROJ,$(OFLUX_PLUGIN_PROJECTS),\
	$(call process_oflux_plugin_project,$(notdir $(PROJ)),$(dir $(PROJ))))
endef

ALL_LIBRARIES :=

$(eval $(process_dirs))
$(eval $(process_oflux_projects))

include $(SRCDIR)/Mk/doc_examples.mk

ALL_TESTS := $(OFLUX_TESTS)
ALL_UNIT_TESTS := $(OFLUX_UNIT_TESTS)
ALL_DOCUMENTATION := $(OFLUX_DOCUMENTATION) 

.PHONY:
	all \
	clean \
	doc \
	unittests

$(ALL_UNIT_TESTS:%.cpp=%): %_unittest : %_unittest.o liboflux.so
	$(CXX) -L. $(CXXOPTS) $(INCS) $(LIBDIRS) $(filter-out liboflux.%,$^) $(LIBS) -loflux -lgtest -o $@

$(ALL_UNIT_TESTS:%.cpp=%.xml): %.xml : %
	LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:. \
	$(CURDIR)/$< --gtest_output="xml:$(CURDIR)/$@"


build: $(ALL_TOOLS) $(ALL_LIBRARIES) $(ALL_APPS) $(ALL_TESTS) unittests

unittests: $(ALL_UNIT_TESTS:%.cpp=%.xml)

clean::
	$(RM) $(ALL_LIBRARIES)

doc: $(ALL_DOCUMENTATION)

#----- END Boilerplate
endif


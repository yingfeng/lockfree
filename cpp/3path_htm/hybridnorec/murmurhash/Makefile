## -*- Makefile -*-
##
## User: trbot
## Time: Apr 23, 2016 12:30:52 AM
## Makefile created by Oracle Solaris Studio.
##
## This file is generated automatically.
##


#### Compiler and tool definitions shared by all build targets #####
CCC = g++
CXX = g++
BASICOPTS = -g
CCFLAGS = $(BASICOPTS)
CXXFLAGS = $(BASICOPTS)
CCADMIN = 


# Define the target directories.
TARGETDIR_murmurtest=.


all: murmurtest

## Target: murmurtest
OBJS_murmurtest =  \
	$(TARGETDIR_murmurtest)/MurmurHash3.o
USERLIBS_murmurtest = $(SYSLIBS_murmurtest) 
DEPLIBS_murmurtest =  
LDLIBS_murmurtest = $(USERLIBS_murmurtest)


# Link or archive
$(TARGETDIR_murmurtest)/murmurtest: $(TARGETDIR_murmurtest) $(OBJS_murmurtest) $(DEPLIBS_murmurtest)
	$(LINK.cc) $(CCFLAGS_murmurtest) $(CPPFLAGS_murmurtest) -o $@ $(OBJS_murmurtest) $(LDLIBS_murmurtest)


# Compile source files into .o files
$(TARGETDIR_murmurtest)/MurmurHash3.o: $(TARGETDIR_murmurtest) MurmurHash3.cpp
	$(COMPILE.cc) $(CCFLAGS_murmurtest) $(CPPFLAGS_murmurtest) -o $@ MurmurHash3.cpp



#### Clean target deletes all generated files ####
clean:
	rm -f \
		$(TARGETDIR_murmurtest)/murmurtest \
		$(TARGETDIR_murmurtest)/MurmurHash3.o
# Enable dependency checking
.KEEP_STATE:
.KEEP_STATE_FILE:.make.state.GNU-amd64-Linux


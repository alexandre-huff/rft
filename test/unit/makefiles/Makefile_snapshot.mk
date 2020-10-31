COMPONENT_NAME=snapshot

# we wrap system calls with the linker
# each required system call needs to be specified here
CPPUTEST_LDFLAGS += -Wl,-wrap,fork -Wl,-wrap,_exit -Wl,-wrap,write -Wl,-wrap,read -Wl,-wrap,waitpid

SRC_FILES = \
  $(PROJECT_SRC_DIR)/snapshot.c \
  $(UNITTEST_ROOT)/mocks/mock_log.cpp \
  $(UNITTEST_ROOT)/mocks/mock_syscalls.cpp \
  $(UNITTEST_ROOT)/mocks/mock_config.cpp \
  $(UNITTEST_ROOT)/mocks/mock_rmr.cpp \
  $(UNITTEST_ROOT)/mocks/mock_pthread.cpp \
  $(UNITTEST_ROOT)/fakes/fake_string.cpp \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_snapshot.cpp

include $(CPPUTEST_MAKFILE_INFRA)

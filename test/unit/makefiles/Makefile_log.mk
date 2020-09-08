COMPONENT_NAME=log

SRC_FILES = \
  $(PROJECT_SRC_DIR)/log.c \
  $(UNITTEST_ROOT)/mocks/mock_config.cpp \
  $(UNITTEST_ROOT)/fakes/fake_string.cpp \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_log.cpp

include $(CPPUTEST_MAKFILE_INFRA)

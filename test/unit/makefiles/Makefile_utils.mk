COMPONENT_NAME=utils

SRC_FILES = \
  $(PROJECT_SRC_DIR)/utils.c \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_utils.cpp

include $(CPPUTEST_MAKFILE_INFRA)

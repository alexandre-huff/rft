COMPONENT_NAME=config

SRC_FILES = \
  $(PROJECT_SRC_DIR)/config.c \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_config.cpp

include $(CPPUTEST_MAKFILE_INFRA)

COMPONENT_NAME=config

SRC_FILES = \
  $(PROJECT_SRC_DIR)/config.c \
  $(UNITTEST_ROOT)/mocks/mock_log.cpp \
  $(UNITTEST_ROOT)/mocks/mock_rft_private.cpp \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_config.cpp

include $(CPPUTEST_MAKFILE_INFRA)

COMPONENT_NAME=rft

SRC_FILES = \
  $(PROJECT_SRC_DIR)/rft.c \
  $(UNITTEST_ROOT)/mocks/mock_config.cpp \
  $(UNITTEST_ROOT)/mocks/mock_log.cpp \
  $(UNITTEST_ROOT)/mocks/mock_snapshot.cpp \
  $(UNITTEST_ROOT)/mocks/mock_rmr.cpp \

TEST_SRC_FILES = \
  $(UNITTEST_SRC_DIR)/test_rft.cpp

include $(CPPUTEST_MAKFILE_INFRA)

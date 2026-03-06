/*
 *  scsi_null.cpp - SCSI null driver (no-op implementation)
 *
 *  Used for headless operation and testing.
 */

#include "sysdeps.h"
#include "platform.h"

/*
 *  Initialization
 */
void scsi_null_init(void)
{
	// No-op
}

/*
 *  Deinitialization
 */
void scsi_null_exit(void)
{
	// No-op
}

/*
 *  Set SCSI command
 */
void scsi_null_set_cmd(int cmd_length, uint8_t *cmd)
{
	// No-op
}

/*
 *  Check for presence of SCSI target
 */
bool scsi_null_is_target_present(int id)
{
	return false;  // No targets present
}

/*
 *  Set SCSI target
 */
bool scsi_null_set_target(int id, int lun)
{
	return false;  // Target not found
}

/*
 *  Send SCSI command
 */
bool scsi_null_send_cmd(size_t data_length, bool reading, int sg_index,
                        uint8_t **sg_ptr, uint32_t *sg_len, uint16_t *stat, uint32_t timeout)
{
	return false;  // Command failed
}

#include <linux/proc_fs.h>
//#include "asus_cam_sensor.h"
#include "asus_actuator.h"
#include "actuator_i2c.h"
#include "cam_actuator_core.h"

#undef  pr_fmt
#define pr_fmt(fmt) "ACTUATOR-ATD %s(): " fmt, __func__

#define	PROC_POWER	"driver/actuator_power"
#define	PROC_I2C_RW	"driver/actuator_i2c_rw"
#define	PROC_VCM_ENABLE	"driver/vcm_enable"
#define	PROC_VCM_VALUE	"driver/vcm0"
#define	PROC_VCM_EEPROM	"driver/vcm_eeprom"
#define	PROC_VCM_SET	"driver/vcm_set"

#define	PROC_VCM_GYRO_OFFSET_SET	"driver/vcm_gyro_offset_set"
#define	PROC_VCM_IC_RW	"driver/vcm_ic_rw"



static struct cam_actuator_ctrl_t * actuator_ctrl = NULL;



static struct mutex g_busy_job_mutex;

uint8_t g_actuator_power_state = 0;
uint8_t g_actuator_camera_open = 0;

static uint8_t g_atd_status = 0;//fail
static uint16_t g_reg_addr = 0x1F; //VCM read SRC mode addr
static uint16_t g_reg_addr_M = 0x03; //VCM read focus data addr MSB
static uint16_t g_reg_addr_L = 0x04; //VCM read focus data addr LSB
static uint32_t g_reg_val = 0;
static uint16_t g_slave_id = 0xC;
static uint16_t g_slave_id_eeprom = 0xC;


static enum camera_sensor_i2c_type g_data_type = CAMERA_SENSOR_I2C_TYPE_WORD; //word
static enum camera_sensor_i2c_type g_addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;

static uint8_t g_operation = 0;//read



void actuator_lock(void)
{
	if(actuator_ctrl == NULL)
	{
		pr_err("ACTUATOR not init!!!\n");
		return;
	}
	mutex_lock(&actuator_ctrl->actuator_mutex);
}

void actuator_unlock(void)
{
	if(actuator_ctrl == NULL)
	{
		pr_err("ACTUATOR not init!!!\n");
		return;
	}
	mutex_unlock(&actuator_ctrl->actuator_mutex);
}

int actuator_busy_job_trylock(void)
{
	if(actuator_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return 0;
	}
	return mutex_trylock(&g_busy_job_mutex);
}

void actuator_busy_job_unlock(void)
{
	if(actuator_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}
	mutex_unlock(&g_busy_job_mutex);
}

static void create_proc_file(const char *PATH, const struct proc_ops* f_ops)
{
	struct proc_dir_entry *pde;

	pde = proc_create(PATH, 0666, NULL, f_ops);
	if(pde)
	{
		pr_info("create(%s) done\n",PATH);
	}
	else
	{
		pr_err("create(%s) failed!\n",PATH);
	}
}


#if 0

int32_t trans_dac_value(uint32_t input_dac)
{
	int32_t HallMax = 0x6000;
	int32_t HallMin = 0xEA80;
	int TotalRange;
	int32_t DAC;

	if(input_dac < 1 || input_dac > 4095)
	{
		CAM_INFO(CAM_ACTUATOR,"Input DAC value is wrong");
		return -1;
	}
	if(input_dac >0) input_dac -=1;

	TotalRange = (HallMax - 0x0000) + (0xFFFF - HallMin)+1;

	DAC = HallMin + ((TotalRange * input_dac)/4094);

	if(DAC > 0xFFFF ) DAC -= 0xFFFF;

	return DAC;
}
/*
int onsemi_actuator_init_setting(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc;
	uint32_t reg_addr[2] = {0xf0, 0xe0};
	uint32_t reg_data;
	uint32_t reg_input_data = 0x1;

	CAM_INFO(CAM_ACTUATOR,"onsemi_actuator_init_setting E\n");

	rc = actuator_read_byte(a_ctrl, reg_addr[0], &reg_data);
	if( rc < 0)
		CAM_INFO(CAM_ACTUATOR,"Read 0x%x failed data 0x%x\n", reg_addr[0], reg_data);

	if( reg_data == 0x42)
	{
		CAM_INFO(CAM_ACTUATOR,"0xf0 data is 0x%x! Right, do next cmd\n", reg_data);

		rc = actuator_write_byte(a_ctrl,reg_addr[1],reg_input_data);
		if(rc == 0)
		{
			CAM_INFO(CAM_ACTUATOR,"Write addr 0x%x data 0x%x success! Start delay\n",
				reg_addr[1], reg_input_data);
			mdelay(5);
			CAM_INFO(CAM_ACTUATOR,"Write addr 0x%x data 0x%x success! End delay\n",
				reg_addr[1], reg_input_data);
		}
		if( rc < 0 )
			CAM_INFO(CAM_ACTUATOR,"Write 0x%x failed\n", reg_addr[1]);
		else
		{
			reg_data = 0x5;
			rc = actuator_read_byte(a_ctrl, reg_addr[1], &reg_data);
			CAM_INFO(CAM_ACTUATOR,"Read 0x%x data 0x%x\n", reg_addr[1], reg_data);
			if(reg_data == 0x0)
				g_actuator_init_setting = 0;
		}
	}
	else
		CAM_INFO(CAM_ACTUATOR,"0xf0 data is not 0x42: 0x%x\n", reg_data);

	CAM_INFO(CAM_ACTUATOR,"onsemi_actuator_init_setting X\n");
	return rc;
}*/

#endif
static int actuator_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val;
	int rc;

	mutex_lock(&actuator_ctrl->actuator_mutex);
	g_actuator_power_state = 1;
	if(g_actuator_power_state)
	{
		//F40_WaitProcess(ois_ctrl,0,__func__);

		pr_err("CHHO sid = 0x%x ", actuator_ctrl->io_master_info.cci_client->sid);
		actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;
		switch(g_data_type)
		{
			case CAMERA_SENSOR_I2C_TYPE_BYTE:
				rc = actuator_read_byte(actuator_ctrl, g_reg_addr, &reg_val, g_addr_type);
				break;
			case CAMERA_SENSOR_I2C_TYPE_WORD:
				rc = actuator_read_word(actuator_ctrl, g_reg_addr, &reg_val, g_addr_type);
				break;
			case CAMERA_SENSOR_I2C_TYPE_DWORD:
				rc = actuator_read_dword(actuator_ctrl, g_reg_addr, &reg_val, g_addr_type);
				break;
			default:
				rc = actuator_read_dword(actuator_ctrl, g_reg_addr, &reg_val, g_addr_type);
		}

		if(g_operation == 1)//write
		{
			if(reg_val == g_reg_val)
			{
				pr_info("read back the same value as write!\n");
			}
			else
			{
				pr_err("write value 0x%x and read back value 0x%x not same!\n",
						g_reg_val, reg_val
				);
			}
		}

		if(rc == 0)
		{
			g_atd_status = 1;
		}
		else
		{
			g_atd_status = 0;
			pr_err("read from reg 0x%x failed! rc = %d\n",g_reg_addr,rc);
		}

		seq_printf(buf,"0x%x\n",reg_val);
	}
	else
	{
		seq_printf(buf,"POWER DOWN\n");
	}

	mutex_unlock(&actuator_ctrl->actuator_mutex);

	return 0;
}

static int actuator_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_i2c_debug_read, NULL);
}

static ssize_t actuator_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	int n;
	char messages[32]="";
	uint32_t val[5];
	int rc;
	int32_t vcm_register_value = 0;

	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages,"%x %x %x %x %x", &val[0], &val[1], &val[2], &val[3], &val[4]);

	mutex_lock(&actuator_ctrl->actuator_mutex);

	if(n == 1)
	{
		g_reg_addr = val[0];
		g_operation = 0;
		g_data_type = 2;//default dword
	}
	else if(n == 2)
	{
		//data type
		// 1 byte 2 word 4 dword
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_operation = 0;
	}
	else if(n == 3)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_operation = 1;
	}
	else if(n == 4)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_slave_id = val[3];//slave id
		g_operation = 1;
	}
	else if(n == 5)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_slave_id = val[3];//slave id
		g_operation = val[4]; //0 read 1 write

		actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;
	}

	if(g_data_type != 1 && g_data_type != 2 && g_data_type != 4 )
		g_data_type = 4;//default dword


	pr_err("CHHO  %s SLAVE 0x%X reg 0x%02x, data type %s\n",
			g_operation ? "WRITE":"READ",
			g_slave_id,
			g_reg_addr,
			g_data_type == 1?"BYTE":(g_data_type == 2?"WORD":"DWORD")
			);

	switch(g_data_type)
	{
		case 1:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			break;
		case 2:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			break;
		case 4:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
			break;
		default:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
	}

	if(g_operation == 1)
	{
		switch(g_data_type)
		{
			case CAMERA_SENSOR_I2C_TYPE_BYTE:

				rc = actuator_write_byte(actuator_ctrl,g_reg_addr,g_reg_val,g_addr_type);
				break;
			case CAMERA_SENSOR_I2C_TYPE_WORD:
				if(g_reg_addr == 0xFFFE)
				{
					g_reg_addr = 0xA0;
					//vcm_register_value = trans_dac_value(g_reg_val);
					rc = actuator_write_word(actuator_ctrl, g_reg_addr, vcm_register_value, g_addr_type);
					//pr_info("Dac value 0x%x register value 0x%04x\n",g_reg_val,vcm_register_value);
				}
				else
					rc = actuator_write_word(actuator_ctrl, g_reg_addr, g_reg_val, g_addr_type);
				break;
			case CAMERA_SENSOR_I2C_TYPE_DWORD:
				rc = actuator_write_dword(actuator_ctrl, g_reg_addr, g_reg_val, g_addr_type);
				break;
			default:
				rc = actuator_write_byte(actuator_ctrl, g_reg_addr, g_reg_val, g_addr_type);
		}
		if(rc < 0)
		{
			pr_err("write 0x%x to reg 0x%04x FAIL\n", g_reg_val, g_reg_addr);
			g_atd_status = 0;
		}
		else
		{
			pr_info("write 0x%x to reg 0x%04x OK\n", g_reg_val, g_reg_addr);
			g_atd_status = 1;
		}
	}

	mutex_unlock(&actuator_ctrl->actuator_mutex);

	return ret_len;
}
static const struct proc_ops actuator_i2c_debug_fops = {
	//.owner = THIS_MODULE,
	.proc_open = actuator_i2c_debug_open,
	.proc_read = seq_read,
	.proc_write = actuator_i2c_debug_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};





static int actuator_allow_vcm_move_read(struct seq_file *buf, void *v)
{
	mutex_lock(&actuator_ctrl->actuator_mutex);
	seq_printf(buf, "%d\n", g_vcm_enabled);
	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return 0;
}

static int actuator_allow_vcm_move_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_allow_vcm_move_read, NULL);
}
static ssize_t actuator_allow_vcm_move_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[8]="";

	uint32_t val;

	ret_len = len;
	if (len > 8) {
		len = 8;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%d", &val);

	mutex_lock(&actuator_ctrl->actuator_mutex);

	if(val == 0)
		g_vcm_enabled = 0;
	else
		g_vcm_enabled = 1;

	pr_info("vcm enabled set to %d\n", g_vcm_enabled);

	mutex_unlock(&actuator_ctrl->actuator_mutex);

	return ret_len;
}

static const struct proc_ops actuator_allow_vcm_move_fops = {
	//.owner = THIS_MODULE,
	.proc_open = actuator_allow_vcm_move_open,
	.proc_read = seq_read,
	.proc_write = actuator_allow_vcm_move_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
static int actuator_read_vcm_dac_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val_M = 0;
	uint32_t reg_val_L = 0;
	uint32_t reg_val = 0;
	int rc1,rc2;

	mutex_lock(&actuator_ctrl->actuator_mutex);
	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;
	rc1 = actuator_read_byte(actuator_ctrl, g_reg_addr_M, &reg_val_M, CAMERA_SENSOR_I2C_TYPE_BYTE);
	rc2 = actuator_read_byte(actuator_ctrl, g_reg_addr_L, &reg_val_L, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if(rc1 != 0)
		pr_err("CHHO_DAC read from reg 0x%x failed! rc = %d\n",g_reg_addr_M,rc1);
	if(rc2 != 0)
		pr_err("CHHO_DAC read from reg 0x%x failed! rc = %d\n",g_reg_addr_L,rc2);

	reg_val = (reg_val_M << 8) + reg_val_L;
	pr_err("CHHO_DAC  vcm_dac_data: 0x%x", reg_val);
	seq_printf(buf,"%d\n",reg_val);
	mutex_unlock(&actuator_ctrl->actuator_mutex);

	return 0;
}

static int actuator_read_vcm_dac_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_read_vcm_dac_read, NULL);
}

static const struct proc_ops actuator_read_vcm_dac_fops = {
		//.owner = THIS_MODULE,
		.proc_open = actuator_read_vcm_dac_open,
		.proc_read = seq_read,
		.proc_lseek = seq_lseek,
		.proc_release = single_release,
};




static int actuator_read_vcm_eeprom_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val = 0;
	int rc,i;

	mutex_lock(&actuator_ctrl->actuator_mutex);

/*
	rc = actuator_write_byte(actuator_ctrl, 0x98, 0xE2, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x98 failed! rc = %d\n", rc);
	}

	rc = actuator_write_byte(actuator_ctrl, 0x99, 0xAE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x99 failed! rc = %d\n", rc);
	}


	rc = actuator_read_byte(actuator_ctrl, 0x98, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	seq_printf(buf,"0x98  0x%02X\n", reg_val);

	rc = actuator_read_byte(actuator_ctrl, 0x99, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	seq_printf(buf,"0x99  0x%02X\n", reg_val);


*/


	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id_eeprom;

/*
	rc = actuator_write_byte(actuator_ctrl, 0x13, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x13 failed! rc = %d\n", rc);
	}
*/


	for (i = 0x00; i <= 0x7F; i++)
	{

		rc = actuator_read_byte(actuator_ctrl, i, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
		if(rc != 0)
		{
			pr_err("CHHO read from reg 0x%x failed! rc = %d\n", i, rc);
		}else
		{
			seq_printf(buf,"0x%04X  0x%02X\n", i, reg_val);
		}
	}





	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;

/*
	rc = actuator_write_byte(actuator_ctrl, 0x98, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x98 failed! rc = %d\n", rc);
	}

	rc = actuator_write_byte(actuator_ctrl, 0x99, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x99 failed! rc = %d\n", rc);
	}

*/
	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return 0;

}



static int actuator_read_vcm_eeprom_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_read_vcm_eeprom_read, NULL);
}

static const struct proc_ops actuator_read_vcm_eeprom_fops = {
		//.owner = THIS_MODULE,
		.proc_open = actuator_read_vcm_eeprom_open,
		.proc_read = seq_read,
		.proc_lseek = seq_lseek,
		.proc_release = single_release,
};

#if 0

static int actuator_read_vcm_set_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val = 0;
	int rc;

	mutex_lock(&actuator_ctrl->actuator_mutex);

	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;

	rc = actuator_write_byte(actuator_ctrl, 0x98, 0xE2, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x98 failed! rc = %d\n", rc);
	}

	rc = actuator_write_byte(actuator_ctrl, 0x99, 0xAE, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x99 failed! rc = %d\n", rc);
	}


	rc = actuator_read_byte(actuator_ctrl, 0x98, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	seq_printf(buf,"0x98  0x%02X\n", reg_val);

	rc = actuator_read_byte(actuator_ctrl, 0x99, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	seq_printf(buf,"0x99  0x%02X\n", reg_val);





	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id_eeprom;


	rc = actuator_write_byte(actuator_ctrl, 0x13, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x13 failed! rc = %d\n", rc);
	}




	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;


	rc = actuator_write_byte(actuator_ctrl, 0x98, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);

	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x98 failed! rc = %d\n", rc);
	}

	rc = actuator_write_byte(actuator_ctrl, 0x99, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc != 0)
	{
		pr_err("CHHO read from reg 0x99 failed! rc = %d\n", rc);
	}


	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return 0;

}



static int actuator_read_vcm_set_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_read_vcm_set_read, NULL);
}

static const struct proc_ops actuator_read_vcm_set_fops = {
		//.owner = THIS_MODULE,
		.proc_open = actuator_read_vcm_set_open,
		.proc_read = seq_read,
		.proc_llseek = seq_lseek,
		.proc_release = single_release,
};

#endif

static int ois_actuator_power_read(struct seq_file *buf, void *v)
{
    mutex_lock(&actuator_ctrl->actuator_mutex);
    pr_info("g_actuator_power_state = %d\n", g_actuator_power_state);
	seq_printf(buf,"%d\n",g_actuator_power_state);
	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return 0;
}

static int actuator_solo_power_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ois_actuator_power_read, NULL);
}

//just for ATD test
static ssize_t actuator_solo_power_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	int val;
	int rc;
	int i;
	struct cam_hw_soc_info         *soc_info = &actuator_ctrl->soc_info;
	struct cam_actuator_soc_private     *soc_private =
		(struct cam_actuator_soc_private *)actuator_ctrl->soc_info.soc_private;

	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;

	ret_len = len;
	if (len > 16) {
		len = 16;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	mutex_lock(&actuator_ctrl->actuator_mutex);
	if(g_actuator_camera_open == 0)
	{
		if(val == 0)
		{
			if(g_actuator_power_state == 1)
			{
				camera_io_release(&(actuator_ctrl->io_master_info));
				rc = cam_sensor_util_power_down(power_info, soc_info);
				if (rc) {
					pr_err("%s: msm_camera_power_down fail rc = %d\n", __func__, rc);
				}
				else
				{
					g_actuator_power_state = 0;
					pr_info("ACTUATOR POWER DOWN\n");
				}
			}
			else
			{
				pr_info("ACTUATOR already power off, do nothing\n");
			}
		}
		else
		{
			if(g_actuator_power_state == 0)
			{
				/* Get Clock */
				for (i = 0; i < soc_info->num_clk; i++) {
					soc_info->clk[i] = clk_get(soc_info->dev,
						soc_info->clk_name[i]);
					if (!soc_info->clk[i]) {
						CAM_ERR(CAM_UTIL, "get failed for %s",
							soc_info->clk_name[i]);
					}
				}
				rc = cam_sensor_core_power_up(power_info, soc_info, NULL);
				if (rc) {
					pr_err("%s: msm_camera_power_up fail rc = %d\n", __func__, rc);
				}
				else
				{
					g_actuator_power_state = 1;
					camera_io_init(&(actuator_ctrl->io_master_info));
					//rc = onsemi_actuator_init_setting(actuator_ctrl); //tmp disable
					if(rc < 0)
					{
					  pr_err("%s: onsemi_actuator_init_setting failed rc = %d", __func__, rc);
					}
					pr_info("ACTUATOR POWER UP\n");
				}
			}
			else
			{
				pr_info("ACTUATOR already power up, do nothing\n");
			}
		}
	}
	else
	{
		pr_err("camera has been opened, can't control actuator power\n");
	}
	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return ret_len;
}
static const struct proc_ops actuator_solo_power_fops = {
	//.owner = THIS_MODULE,
	.proc_open = actuator_solo_power_open,
	.proc_read = seq_read,
	.proc_write = actuator_solo_power_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//==============================================================


static int actuator_offset_read(struct seq_file *buf, void *v)
{
    //set
	int rc, i;
	uint32_t reg_val, reg_val_L, reg_val_U, reg_val1;
	int32_t FC_ave = 0, reg_val_FC = 0, offset_data = 0;

	mutex_lock(&actuator_ctrl->actuator_mutex);

	rc = actuator_read_byte(actuator_ctrl, 0x00, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	pr_err("CHHO_OFFSET name read 0x00 data:0x%x", reg_val);

	if (reg_val != 0x66) {
	    pr_err("wrond module or i2c error !!\n");
	    return -EFAULT;
	}

	rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE); //stop function
	rc = actuator_write_byte(actuator_ctrl, 0x40, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);

    for (i=0 ; i<256; i++)
    {
        pr_err("------ CHHO_OFFSET Iteration %d ------", i);
        //data = (0x44,0x45,0x46,0x47)
        rc = actuator_read_byte(actuator_ctrl, 0x44, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
        pr_err("CHHO_OFFSET  0x44 =>  0x%x", reg_val);
        rc = actuator_read_byte(actuator_ctrl, 0x45, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
        pr_err("CHHO_OFFSET  0x45 =>  0x%x", reg_val);

		//use only 46 and 47
        rc = actuator_read_byte(actuator_ctrl, 0x46, &reg_val_U, CAMERA_SENSOR_I2C_TYPE_BYTE);
		pr_err("CHHO_OFFSET  0x46 =>  0x%x", reg_val_U);
        rc = actuator_read_byte(actuator_ctrl, 0x47, &reg_val_L, CAMERA_SENSOR_I2C_TYPE_BYTE);
		pr_err("CHHO_OFFSET  0x47 =>  0x%x", reg_val_L);
        
		reg_val_L = reg_val_L & 0x00FF;

		reg_val_FC = (reg_val_U<<8|reg_val_L) & 0xFFFF;

		if (reg_val_FC & 0x8000)
		{
			FC_ave = FC_ave - (((~reg_val_FC) & 0xFFFF) + 1);
		}else
		{
				FC_ave = FC_ave + reg_val_FC;
		}
    		//pr_err("CHHO Read Z data_16:0x%x data_10:%d  reg_val_U 0x%x   reg_val_L 0x%x    FC_ave 0x%x ", reg_val_FC, reg_val_FC, reg_val_U, reg_val_L, FC_ave);
    		//vcm_acc_data[i]=reg_val_FC;
	}

	FC_ave = (FC_ave >> 8) & 0xFFFF;// FC_ave/256

	offset_data = FC_ave;

	vcm_offset_data[0] = (offset_data >> 8) & 0xFF;
	vcm_offset_data[1] = offset_data & 0xFF;

	pr_err("CHHO_OFFSET   offset_data 0x%x   vcm_offset_data[0]: 0x%x  vcm_offset_data[1]: 0x%x", offset_data, vcm_offset_data[0], vcm_offset_data[1]);

	rc = actuator_write_byte(actuator_ctrl, 0x95, vcm_offset_data[0], CAMERA_SENSOR_I2C_TYPE_BYTE);
	rc = actuator_write_byte(actuator_ctrl, 0x96, vcm_offset_data[1], CAMERA_SENSOR_I2C_TYPE_BYTE);

	mdelay(2);

	rc = actuator_read_byte(actuator_ctrl, 0x95, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	pr_err("CHHO_OFFSET  vcm_offset_data addr:0x95   data: 0x%x", reg_val);
	rc = actuator_read_byte(actuator_ctrl, 0x96, &reg_val1, CAMERA_SENSOR_I2C_TYPE_BYTE);
	pr_err("CHHO_OFFSET  vcm_offset_data addr:0x96   data: 0x%x", reg_val1);


	if (vcm_offset_data[0] != reg_val || vcm_offset_data[1] != reg_val1 )
	{
		seq_printf(buf,"fail \n");
	}else
	{
		seq_printf(buf,"PASS %d\n",offset_data);
	}

    rc = actuator_write_byte(actuator_ctrl, 0x02, 0x8A, CAMERA_SENSOR_I2C_TYPE_BYTE);
    rc = actuator_write_byte(actuator_ctrl, 0x1C, 0x80, CAMERA_SENSOR_I2C_TYPE_BYTE);
    //rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE);
    rc = actuator_write_byte(actuator_ctrl, 0x03, 0x02, CAMERA_SENSOR_I2C_TYPE_BYTE);
    rc = actuator_write_byte(actuator_ctrl, 0x04, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);
    mdelay(20);
    rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x03, CAMERA_SENSOR_I2C_TYPE_BYTE); //start function

	mutex_unlock(&actuator_ctrl->actuator_mutex);

#if 0
	FILE* pFile = fopen("/data/vendor/camera/vcm_gyro1.bin", "wb+")
//	FILE* pFile2 = fopen("/data/vendor/camera/vcm_gyro1.bin", "wb+")

    if (NULL != pFile){
        fwrite(vcm_offset_data[0], sizeof(vcm_offset_data[0])-1, 1, pFile);
        fwrite(vcm_offset_data[1], sizeof(vcm_offset_data[1])-1, 1, pFile);
        fclose(pFile);
    }else{
        MY_LOGE("CHHO_OFFSET: OFFSET Debug data failed to open for writing");
    }
#endif

	return 0;

}

static ssize_t actuator_offset_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{

    // 1: up  2: down
	ssize_t ret_len;
	int n;
	char messages[32]="";
	int val, rc;
//	uint32_t reg_addr, reg_val;
    uint32_t reg_val;
//    uint32_t reg_data;

#if 0
	uint32_t reg_val_L, reg_val_U;
	int32_t FC_ave = 0, reg_val_FC = 0, offset_data = 0;
    int i;
#endif

	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}
	n = sscanf(messages,"%x ", &val);


	rc = actuator_read_byte(actuator_ctrl, 0x00, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
	pr_err("CHHO_OFFSET name read 0x00 data:0x%x", reg_val);

	if (reg_val != 0x66) {
	    pr_err("wrond module or i2c error !!\n");
	    return -EFAULT;
	}

	mutex_lock(&actuator_ctrl->actuator_mutex);
	rc = actuator_write_byte(actuator_ctrl, 0x40, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);

    if (val == 1)
    {


		rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE); //stop function
		rc = actuator_write_byte(actuator_ctrl, 0x40, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);

		pr_err("CHHO_OFFSET    vcm_offset_data[0]: 0x%x  vcm_offset_data[1]: 0x%x", vcm_offset_data[0], vcm_offset_data[1]);

		rc = actuator_write_byte(actuator_ctrl, 0x95, vcm_offset_data[0], CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = actuator_write_byte(actuator_ctrl, 0x96, vcm_offset_data[1], CAMERA_SENSOR_I2C_TYPE_BYTE);

		mdelay(2);

		rc = actuator_read_byte(actuator_ctrl, 0x95, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
		pr_err("CHHO_OFFSET  vcm_offset_data addr:0x95   data: 0x%x", reg_val);
		rc = actuator_read_byte(actuator_ctrl, 0x96, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
		pr_err("CHHO_OFFSET  vcm_offset_data addr:0x96   data: 0x%x", reg_val);



		rc = actuator_write_byte(actuator_ctrl, 0x02, 0x8A, CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = actuator_write_byte(actuator_ctrl, 0x1C, 0x80, CAMERA_SENSOR_I2C_TYPE_BYTE);
		//rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x01, CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = actuator_write_byte(actuator_ctrl, 0x03, 0x02, CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = actuator_write_byte(actuator_ctrl, 0x04, 0x00, CAMERA_SENSOR_I2C_TYPE_BYTE);
		mdelay(20);
		rc = actuator_write_byte(actuator_ctrl, 0x1F, 0x03, CAMERA_SENSOR_I2C_TYPE_BYTE); //start function



    }
	mutex_unlock(&actuator_ctrl->actuator_mutex);
	return ret_len;
}

static int actuator_offset_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_offset_read, NULL);
}

static const struct proc_ops actuator_offset_set_fops= {
	//.owner = THIS_MODULE,
	.proc_open = actuator_offset_open,
	.proc_read = seq_read,
	.proc_write = actuator_offset_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

//==============================================================

static int actuator_IC_read(struct seq_file *buf, void *v)
{

	uint32_t reg_data;
	int rc;

	mutex_lock(&actuator_ctrl->actuator_mutex);
	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id_eeprom;

	rc = actuator_read_byte(actuator_ctrl, g_reg_addr, &reg_data, CAMERA_SENSOR_I2C_TYPE_BYTE);
	pr_err("VCM eeprom data addr:0x%x data:0x%x", g_reg_addr, reg_data);

	actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;

	mutex_unlock(&actuator_ctrl->actuator_mutex);

	seq_printf(buf,"addr:0x%x data:0x%x\n", g_reg_addr, reg_data);

	return 0;

}



static ssize_t actuator_IC_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	int n;
	char messages[32]="";
	uint32_t val[5];
	int rc;
	uint32_t reg_addr, reg_data, reg_val, reg_dac1, reg_dac2, reg_val1, reg_val2;
	uint32_t reg_data_lsb, reg_data_msb;


	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages,"%x %x %x", &val[0], &val[1], &val[2]);

	pr_err("VCM eeprom data %d\n", n);

	mutex_lock(&actuator_ctrl->actuator_mutex);

	if (n==3) //write DAC of vcm
	{
		actuator_ctrl->io_master_info.cci_client->sid = g_slave_id_eeprom;
		reg_dac1 = val[0];
		reg_dac2 = val[1];
		reg_data = val[2];
		pr_err("VCM DAC data addr:0x%x",reg_data);
		if(reg_dac1 == g_reg_addr_M && reg_dac2 == g_reg_addr_L)
		{
			reg_data_lsb = reg_data & 0x00FF;
			reg_data_msb = reg_data >> 8;

			rc = actuator_write_byte(actuator_ctrl, reg_dac1, reg_data_msb, CAMERA_SENSOR_I2C_TYPE_BYTE);
			rc = actuator_write_byte(actuator_ctrl, reg_dac2, reg_data_lsb, CAMERA_SENSOR_I2C_TYPE_BYTE);
			msleep(20);

			rc = actuator_read_byte(actuator_ctrl, reg_dac1, &reg_val1, CAMERA_SENSOR_I2C_TYPE_BYTE);
			rc = actuator_read_byte(actuator_ctrl, reg_dac2, &reg_val2, CAMERA_SENSOR_I2C_TYPE_BYTE);
			pr_err("VCM DAC data addr:0x%x data:0x%x",reg_dac1 , reg_val1);
			pr_err("VCM DAC data addr:0x%x data:0x%x",reg_dac2 , reg_val2);

			if (reg_data_msb != reg_val1 || reg_data_lsb != reg_val2)
				pr_err("VCM DAC data write check FAIL\n");
			else
				pr_err("VCM DAC data write check PASS\n");


			actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;

		}
		else
			pr_err("Don't support 3 parameters in VCM_RW\n");
	}
	else if (n==2)
	{
		actuator_ctrl->io_master_info.cci_client->sid = g_slave_id_eeprom;
		reg_addr = val[0];
		reg_data = val[1];

		rc = actuator_write_byte(actuator_ctrl, reg_addr, reg_data, CAMERA_SENSOR_I2C_TYPE_BYTE);
		msleep(20);

		rc = actuator_read_byte(actuator_ctrl, reg_addr, &reg_val, CAMERA_SENSOR_I2C_TYPE_BYTE);
		pr_err("VCM eeprom data addr:0x%x data:0x%x",reg_addr , reg_val);

		if (reg_data != reg_val)
			pr_err("VCM eeprom data write check FAIL\n");
		else
			pr_err("VCM eeprom data write check PASS\n");


		actuator_ctrl->io_master_info.cci_client->sid = g_slave_id;

	}
	else if(n==1)
	{
		g_reg_addr = val[0];
	}

	mutex_unlock(&actuator_ctrl->actuator_mutex);

	return ret_len;

}


static int actuator_IC_open(struct inode *inode, struct  file *file)
{
	return single_open(file, actuator_IC_read, NULL);
}


static const struct proc_ops actuator_IC_RW_fops = {
		//.owner = THIS_MODULE,
		.proc_open = actuator_IC_open,
		.proc_read = seq_read,
		.proc_write = actuator_IC_write,
		.proc_lseek = seq_lseek,
		.proc_release = single_release,
};


#if 0
int actuator_power_up(struct cam_actuator_ctrl_t *actuator_ctrl)
{

	int rc;
	struct cam_hw_soc_info         *soc_info = &actuator_ctrl->soc_info;
	struct cam_actuator_soc_private     *soc_private =
		(struct cam_actuator_soc_private *)actuator_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;


	rc = cam_sensor_core_power_up(power_info, soc_info,NULL);
	if (rc) {
		pr_err("actuator power up failed, rc %d\n", rc);
		return -1;
	}
	if (actuator_ctrl->io_master_info.master_type == CCI_MASTER)
	{
		rc = camera_io_init(&(actuator_ctrl->io_master_info));
		if (rc < 0) {
			pr_err("cci init failed!\n");
			rc = cam_sensor_util_power_down(power_info, soc_info);
			if (rc) {
				pr_err("actuator power down failed, rc %d\n", rc);
			}
			return -2;
		}
	}
	pr_err("Actuator probe power up!");
	return rc;
}

int actuator_power_down(struct cam_actuator_ctrl_t *actuator_ctrl)
{

	int rc;
	struct cam_hw_soc_info         *soc_info = &actuator_ctrl->soc_info;
	struct cam_actuator_soc_private     *soc_private =
		(struct cam_actuator_soc_private *)actuator_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;


    pr_err("actuator_power_down :E");
	if(actuator_ctrl->io_master_info.master_type == CCI_MASTER)
	{
		rc = camera_io_release(&(actuator_ctrl->io_master_info));
		if (rc < 0)
			pr_err("cci release failed!\n");
	}

	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc) {
		pr_err("actuator power down failed, rc %d\n", rc);
	}
	pr_err("Actuator probe power down!");
	return rc;
}

void actuator_probe_check(void)
{
	if(actuator_ctrl == NULL)
	{
		pr_err("actuator_ctrl is NULL!!!\n");
		return;
	}
	if(actuator_power_up(actuator_ctrl) != 0)
	{
		pr_err("actuator power up failed\n");
		return;
	}

	mdelay(1000);

	actuator_power_down(actuator_ctrl);
}

uint8_t asus_allow_vcm_move(void)
{
	return g_vcm_enabled;
}

#endif

static void create_actuator_proc_files_factory(void)
{
	static uint8_t has_created = 0;

	if(!has_created)
	{
		create_proc_file(PROC_POWER, &actuator_solo_power_fops);
		create_proc_file(PROC_I2C_RW,&actuator_i2c_debug_fops);//ATD

		create_proc_file(PROC_VCM_ENABLE, &actuator_allow_vcm_move_fops);
		create_proc_file(PROC_VCM_VALUE, &actuator_read_vcm_dac_fops);

		create_proc_file(PROC_VCM_EEPROM, &actuator_read_vcm_eeprom_fops);

		create_proc_file(PROC_VCM_GYRO_OFFSET_SET, &actuator_offset_set_fops);
        create_proc_file(PROC_VCM_IC_RW, &actuator_IC_RW_fops);
	#if 0
		create_proc_file(PROC_VCM_SET, &actuator_read_vcm_set_fops);
		//create_proc_file(PROC_DSI_CHECK, &cam_csi_check_fops);  //ASUS_BSP Bryant "Add for camera csi debug"
	#endif
		has_created = 1;
	}
	else
	{
		pr_err("ACTUATOR factory proc files have already created!\n");
	}
}

void asus_actuator_init(struct cam_actuator_ctrl_t * ctrl)
{
	if(ctrl)
		actuator_ctrl = ctrl;
	else
	{
		pr_err("actuator_ctrl_t passed in is NULL!\n");
		return;
	}
	pr_err("asus_actuator_init :E create_actuator_proc_files_factory \n");
	create_actuator_proc_files_factory();
	mutex_init(&g_busy_job_mutex);
}


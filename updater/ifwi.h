typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

struct IA32_rev{
	uint8 reserved;
	uint8 minor;
	uint8 major;
	uint8 checksum;
};

struct Punit_rev{
	uint8 reserved;
	uint8 minor;
	uint8 major;
	uint8 checksum;
};

struct OEM_rev{
	uint8 reserved;
	uint8 minor;
	uint8 major;
	uint8 checksum;
};

struct suppIA32_rev{
	uint8 reserved;
	uint8 minor;
	uint8 major;
	uint8 checksum;
};

struct SCU_rev{
	uint8 reserved;
	uint8 minor;
	uint8 major;
	uint8 checksum;
};

struct Chaabi_rev{
	uint32 icache;
	uint32 resident;
	uint32 ext;
};

struct IFWI_rev{
	uint8 minor;
	uint8 major;
	uint16 reserved;
};

struct FIP_header{
	uint32 FIP_SIG;
	uint32 header_info;
	struct IA32_rev ia32_rev;
	struct Punit_rev punit_rev;
	struct OEM_rev oem_rev;
	struct suppIA32_rev suppia32_rev;
	struct SCU_rev scu_rev;
	struct Chaabi_rev chaabi_rev;
	struct IFWI_rev ifwi_rev;
};

struct scu_ipc_version {
	uint32  count;  /* length of version info */
	uint8   data[16]; /* version data */
};

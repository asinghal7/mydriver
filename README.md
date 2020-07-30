# Custom Linux Device Drivers
These are Linux Character Device Drivers capable of user space interaction with the kernel space through text read/write/conversion functions using procfs and sysfs
# mydriver_char.c
This module uses procfs for user space - kernel space interaction. This provides the read/write ability to the user into the char device.
'forceupper' branch provides the utility to convert a lowercase user input string into an uppercase string.
# mydriver_sysfs.c
This module uses sysfs for user space - kernel space interaction. It provides the option to choose different modes of conversion (normal, uppercase, ROT13 encryption/decryption) to user input string.

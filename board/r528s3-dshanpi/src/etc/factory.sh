set +e

echo "umount /data"
#ifdef CONFIG_FS_YAFFS
umount /data
echo "force format /dev/data"
mount -t yaffs -o forceformat /dev/usrdata /data
#else
echo "CONFIG_FS_YAFFS is disabled, skip /data forceformat"
mkdir /data
#endif

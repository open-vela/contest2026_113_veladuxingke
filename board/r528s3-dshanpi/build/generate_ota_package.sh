#/bin/bash

function get_old_img {
    cd ${TOP}
    wget -O ${old_dir}.tar.gz $ota_base_url
    tar -xf ${old_dir}.tar.gz
}

function ota_package {
    product=$1
    configs=("ap" "ota")
    tmp_configs=("ap" "ap_test" "ap_performance_test" "ap_stability_test")
    pack_work_dir=${TOP}/vendor/allwinnertech/lichee
    dest_dir=${TOP}/vendor/allwinnertech/lichee/board/r528s3/dshanpi_nand/configs

    cp ${copyPath}/${product}/bootloader/bl.fex ${copyPath}/${product}/ota/ota.fex ${dest_dir}

    for config in ${tmp_configs[@]}; do
        cp ${copyPath}/${product}/$config/ap.fex ${dest_dir}
        cd ${pack_work_dir}
        bash ./pack.sh r528s3-dshanpi
        if [ $config == "ap" ]; then
            config=ap_default
        fi
        image_dir=image`echo $config | awk -F '_' '{print "_"$2}'`
        mkdir ${copyPath}/${product}/${image_dir}
        find out/ -name '*.img' -exec cp {} ${copyPath}/${product}/${image_dir}/ \;
        rm -rf out
    done

    cd $TOP
    if [ -d ota_imgs ]; then
        rm -rf ota_imgs
    fi
    mkdir -p ota_imgs
    for config in ${configs[@]}; do
        cp ${copyPath}/${product}/${config}/vela_${config}.bin ota_imgs
    done
    ./frameworks/ota/tools/gen_ota_zip.py ota_imgs --skip_version_check --debug --sign
    mv ota.zip $TOP/${RELEASE_ID}/images/${product}/

    #ota_diff
    if [ -d ota_imgs ]; then
        rm -rf ota_imgs
    fi
    unzip -d $TOP/ota_imgs $TOP/${RELEASE_ID}/images/${product}/ota.zip
    old_dir=`echo ${ota_base_url##*/} | awk -F'.tar' '{print $1}'`
    if [ "$old_dir"X != ""X ]; then
        get_old_img
        if [ -d $old_dir ]; then
            if [ -d old_ota_imgs ]; then
                rm -rf old_ota_imgs
            fi
            unzip -d $TOP/old_ota_imgs ${old_dir}/images/${product}/ota.zip
            rm -rf old_ota_imgs/META-INF/ ota_imgs/META-INF/
            ./frameworks/ota/tools/gen_ota_zip.py old_ota_imgs ota_imgs --skip_version_check --debug --sign --blksz 1048576
            mv ota.zip $TOP/${RELEASE_ID}/images/${product}/ota_${old_dir}-${RELEASE_ID}.zip
            cp ${old_dir}/images/${product}/ota.zip $TOP/${RELEASE_ID}/images/${product}/ota_${old_dir}.zip
        fi
    fi
}

TOP=$1
RELEASE_ID=$2
product=$3
ota_base_url=$4
copyPath="$TOP/${RELEASE_ID}/images"

ota_package ${product}

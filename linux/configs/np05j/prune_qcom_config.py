import re

keep = {
    "CONFIG_COMMON_CLK_QCOM", "CONFIG_QCOM_GDSC", "CONFIG_QCOM_CLK_RPMH",
    "CONFIG_SM_GCC_8750", "CONFIG_SM_TCSRCC_8750",
    "CONFIG_PINCTRL_MSM", "CONFIG_PINCTRL_SM8750", "CONFIG_PINCTRL_QCOM_SPMI_PMIC",
    "CONFIG_INTERCONNECT_QCOM", "CONFIG_INTERCONNECT_QCOM_RPMH",
    "CONFIG_INTERCONNECT_QCOM_BCM_VOTER", "CONFIG_INTERCONNECT_QCOM_SM8750",
    "CONFIG_INTERCONNECT_QCOM_RPMH_POSSIBLE",
}

with open("/tmp/np05j-linux7/build/.config") as f:
    lines = f.readlines()

new_lines = []
pruned = 0
pat = re.compile(r"^(CONFIG_(?:(?:IPQ|MSM|QCM|QCS|SA|SC|SDM|SDX|SM|X1E|KAANAPALI|MILOS|SAR|QDU|QRU|MDM|APQ|KODIAK)_|PINCTRL_(?:IPQ|MSM|QCM|QCS|SA|SC|SDM|SDX|SM|X1E|KAANAPALI|MILOS|SAR|QDU|MDM|APQ|LPASS)|INTERCONNECT_QCOM_(?:IPQ|MSM|QCM|QCS|SA|SC|SDM|SDX|SM|X1E|KAANAPALI|MILOS|SAR|QDU|OSM)|QCOM_(?:A53PLL|CLK_APCC|CLK_SMD|CLK_RPM|HFPLL|EBI2|PAS|RPM_))[A-Z0-9_]*)=y$")
for line in lines:
    m = pat.match(line.strip())
    if m and m.group(1) not in keep:
        new_lines.append(f"# {m.group(1)} is not set\n")
        pruned += 1
    else:
        new_lines.append(line)

with open("/tmp/np05j-linux7/build/.config", "w") as f:
    f.writelines(new_lines)
print(f"Pruned {pruned} unrelated Qualcomm SoC drivers")

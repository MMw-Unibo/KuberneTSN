// K8S TSN-CNI PLUGIN for KuberneTSN Andrea Barigazzi  CNI spec 1.1.0
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"github.com/containernetworking/cni/pkg/skel"
	"github.com/containernetworking/cni/pkg/types"
	current "github.com/containernetworking/cni/pkg/types/100"
	"github.com/containernetworking/cni/pkg/version"
	"github.com/containernetworking/plugins/pkg/ipam"
	bv "github.com/containernetworking/plugins/pkg/utils/buildversion"
	"github.com/vishvananda/netlink"
	"github.com/vishvananda/netns"
	"log"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

// Declare logger
var logger, _ = setupLogger("/var/log/tsncni-plugin.log")

// Config logger
func setupLogger(logFilePath string) (*log.Logger, error) {
	file, err := os.OpenFile(logFilePath, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		return nil, err
	}
	logger := log.New(file, "CNI-DEBUG: ", log.Ldate|log.Ltime|log.Lshortfile)
	return logger, nil
}

// PluginConf  NetConf + RuntimeCustomConfs
type PluginConf struct {
	types.NetConf
	RuntimeConfig *struct {
		RunConfig map[string]interface{} `json:"sample"`
	} `json:"runtimeConfig"`
	// Plugin-specific
	OvsBridge    string `json:"OvsBridge"`
	ProvisionOvs bool   `json:"ProvisionOvs"`
}

func parseConfig(stdin []byte) (*PluginConf, error) {
	conf := PluginConf{}
	if err := json.Unmarshal(stdin, &conf); err != nil {
		return nil, fmt.Errorf("failed to parse network configuration: %v", err)
	}
	if conf.OvsBridge == "" {
		return nil, fmt.Errorf("OvsBridge must be specified")
	}
	return &conf, nil
}

// cmdAdd for ADD requests
func cmdAdd(args *skel.CmdArgs) error {
	conf, err := parseConfig(args.StdinData)
	if err != nil {
		return err
	}
	//CALL IPAM PLUGIN==================================================================================================
	configString := arrbyteToString(args.StdinData) //StdinData è un array di byte...
	ipamOutput, err := callIpamPlugin(configString, args.ContainerID, args.Netns, args.IfName, "ADD")
	var ipamOutputJson map[string]interface{}
	ipamOutputStr, err := json.MarshalIndent(ipamOutputJson, "", "  ")
	if err != nil {
		logger.Printf("Errore nella serializzazione del JSON: %v", err)
		return err
	}
	logger.Printf("IPAM HOST-LOCAL OUTPUT JSON:\n%s", string(ipamOutputStr))
	//endCALL IPAM =====================================================================================================

	// CHECK IF CHAINED PLUGIN
	if conf.PrevResult != nil {
		return fmt.Errorf("must be called as the first plugin")
	}
	//==================================================================================================================
	//==================================================================================================================
	// Check OVS status, create if needed...
	//assert_or_create_ovs_bridge()
	//==================================================================================================================
	err = json.Unmarshal([]byte(ipamOutput), &ipamOutputJson)
	if err != nil {
		return err
	}
	ip, cidr, gw, _ := getIpamNetData(ipamOutputJson) // get ip, cidr and gw from ipam output
	cidrInt, _ := strconv.Atoi(cidr)
	bridge := conf.OvsBridge
	ofPortNum := "1" // openflow port, dinamico?
	podNetNamespace := args.Netns

	logger.Printf("Ipam plugin result: %v %v %v", ip, cidr, gw)
	logger.Printf("pod_net_namespace: %v", podNetNamespace)
	logger.Printf("All args: %v", args)

	ovsPort, containerPort := createVethPair(args.ContainerID)

	logger.Printf("ovs_port: %v", ovsPort)
	logger.Printf("container_port: %v", containerPort)

	setOvsPort(bridge, ovsPort, ofPortNum)
	//setPodPort(containerPort, podNetNamespace, ip, cidr, gw)

	realMac, err := setPodPort(containerPort, podNetNamespace, ip, cidr, gw)
	if err != nil {
		return err
	}
	// add to the result
	result := &current.Result{CNIVersion: current.ImplementedSpecVersion}
	result.Interfaces = []*current.Interface{
		{
			Name:    containerPort,
			Sandbox: args.Netns,
			Mac:     realMac,
		},
	}
	result.IPs = []*current.IPConfig{
		{
			Address: net.IPNet{
				IP:   net.ParseIP(ip),
				Mask: net.CIDRMask(cidrInt, 32),
			},
			Gateway:   net.ParseIP(gw),
			Interface: current.Int(0),
		},
	}
	logger.Printf("Result: %v", result)
	// Pass through the result for the next plugin
	return types.PrintResult(result, conf.CNIVersion)
}

// cmdDel DELETE requests
func cmdDel(args *skel.CmdArgs) error {
	conf, err := parseConfig(args.StdinData)
	if err != nil {
		return err
	}
	//Delete veth inside container===================================================
	// Verifica se netns è vuoto il container è già stato eliminato
	if args.Netns == "" {
		logger.Printf("Il namespace di rete non esiste più, nessuna azione necessaria.")
	} else {
		// Verifica se il namespace esiste prima di tentare di rimuovere l'interfaccia
		cmd := exec.Command("nsenter", "--net="+args.Netns, "ip", "link", "show")
		if err := cmd.Run(); err != nil {
			// Se il comando fallisce, significa che il namespace non esiste
			logger.Printf("Il namespace di rete %s non esiste. Nessuna azione necessaria.", args.Netns)
		} else {
			// Se il namespace esiste, procedi con la rimozione dell'interfaccia
			cmd = exec.Command("nsenter", "--net="+args.Netns, "ip", "link", "delete", args.IfName)
			if err := cmd.Run(); err != nil {
				logger.Printf("Errore durante la rimozione dell'interfaccia %s: %v", args.IfName, err)
			}
		}
	}
	//Delete veth from OVS bridge=====================================================
	// Delete veth from OVS bridge=====================================================
	bridge := conf.OvsBridge
	containerId := args.ContainerID
	id := containerId[len(containerId)-8:]
	ovsPort := "veth" + id

	// Verifica se il bridge esiste
	cmd := exec.Command("ovs-vsctl", "list-br", bridge)
	if err := cmd.Run(); err != nil {
		logger.Printf("Il bridge OVS %s non esiste. Nessuna azione necessaria per la porta %s.", bridge, ovsPort)
	} else {
		// Verifica se la porta esiste nel bridge
		cmd = exec.Command("ovs-vsctl", "list-ports", bridge)
		output, err := cmd.CombinedOutput()
		if err != nil {
			logger.Printf("Errore durante la verifica delle porte del bridge %s: %v", bridge, err)
		} else if !strings.Contains(string(output), ovsPort) {
			// Se la porta non esiste, non fare nulla
			logger.Printf("La porta %s non esiste nel bridge %s. Nessuna azione necessaria.", ovsPort, bridge)
		} else {
			// Se il bridge e la porta esistono, cancella la porta dal bridge
			cmd = exec.Command("ovs-vsctl", "del-port", bridge, ovsPort)
			if err := cmd.Run(); err != nil {
				logger.Printf("Errore durante la rimozione della porta %s dal bridge %s: %v", ovsPort, bridge, err)
			} else {
				logger.Printf("Deleted port %v from ovs bridge %v.", ovsPort, bridge)
			}
		}
	}
	//==================================================================================================================
	//libera IP assegnato dal plugin IPAM
	configString := arrbyteToString(args.StdinData)
	_, err = callIpamPlugin(configString, args.ContainerID, args.Netns, args.IfName, "DEL")
	logger.Printf("IPAM HOST-LOCAL: IP DELETED") // se nil tutto ok
	return nil
}

func main() {
	bv.BuildVersion = "1.0m"
	skel.PluginMainFuncs(skel.CNIFuncs{
		Add:    cmdAdd,
		Check:  cmdCheck,
		Del:    cmdDel,
		Status: cmdStatus,
	}, version.All, bv.BuildString("TSNCNI"))

}

func cmdCheck(_ *skel.CmdArgs) error {
	// TODO: implement
	return fmt.Errorf("not implemented")
}

func cmdStatus(args *skel.CmdArgs) error {
	conf, err := parseConfig(args.StdinData)
	if err != nil {
		return err
	}
	_ = conf
	if err := ipam.ExecStatus(conf.IPAM.Type, args.StdinData); err != nil {
		return err
	}
	// TODO: implement STATUS here
	return nil
}

func createVethPair(containerId string) (string, string) {
	id := containerId[len(containerId)-8:]
	ovsPort := "veth" + id
	containerPort := "veth" + id + "_p"
	cmd := exec.Command("ip", "link", "add", ovsPort, "type", "veth", "peer", "name", containerPort)
	err := cmd.Run()
	if err != nil {
		return "", ""
	}
	return ovsPort, containerPort
}

func setOvsPort(bridge string, port string, ofPortNum string) {
	cmd := exec.Command("ip", "link", "set", port, "up")
	err := cmd.Run()
	if err != nil {
		return
	}

	cmd = exec.Command("ovs-vsctl", "add-port", bridge, port, "--", "set", "interface", port, "ofport_request="+ofPortNum)
	err = cmd.Run()
	if err != nil {
		logger.Printf("set_ovs_port: %v", err)
		os.Exit(0)
	}
}

func setPodPort(port string, podNetNamespace string, ip string, cidr string, gw string) (string, error) {
	// Sposta la veth_p nel namespace del Pod
	cmd := exec.Command("ip", "link", "set", port, "netns", podNetNamespace)
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("failed to move %s to netns %s: %v", port, podNetNamespace, err)
	}

	// Abilita interfaccia
	cmd = exec.Command("nsenter", "--net="+podNetNamespace, "ip", "link", "set", port, "up")
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("failed to set %s up: %v", port, err)
	}

	// Aggiungi IP
	cmd = exec.Command("nsenter", "--net="+podNetNamespace,
		"ip", "addr", "add", ip+"/"+cidr, "dev", port)
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("failed to add IP to %s: %v", port, err)
	}

	// Aggiungi rotta predefinita
	cmd = exec.Command("nsenter", "--net="+podNetNamespace,
		"ip", "route", "replace", "default", "via", gw)
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("failed to add route in pod ns: %v", err)
	}

	// Disabilita offload (opzionale)
	_ = exec.Command("nsenter", "--net="+podNetNamespace,
		"ethtool", "--offload", port, "rx", "off", "tx", "off").Run()

	// --- Recupero del MAC reale nel namespace del Pod ---
	// Salva namespace corrente
	currNs, err := netns.Get()
	if err != nil {
		return "", fmt.Errorf("failed to get current ns: %v", err)
	}
	// Silenzio l'errore di Close
	defer func() { _ = currNs.Close() }()

	// Entra nel namespace del Pod
	podNs, err := netns.GetFromPath(podNetNamespace)
	if err != nil {
		return "", fmt.Errorf("failed to open pod netns %s: %v", podNetNamespace, err)
	}
	// Silenzio l'errore di Close su podNs
	defer func() { _ = podNs.Close() }()

	// Cambia namespace (silenzio eventuale errore di Set)
	if err := netns.Set(podNs); err != nil {
		return "", fmt.Errorf("failed to enter pod ns: %v", err)
	}
	// Assicuro il ritorno al namespace host, silenziando l'errore
	defer func() { _ = netns.Set(currNs) }()

	// Leggi l'interfaccia e il suo MAC
	link, err := netlink.LinkByName(port)
	if err != nil {
		return "", fmt.Errorf("failed to lookup link %s: %v", port, err)
	}
	realMac := link.Attrs().HardwareAddr.String()
	// ----------------------------------------------------

	return realMac, nil
}

func arrbyteToString(bs []byte) string {
	b := make([]byte, len(bs))
	for i, v := range bs {
		b[i] = byte(v)
	}
	return string(b)
}

func callIpamPlugin(config string, containerid string, netns string, ifname string, cnicommand string) ([]byte, error) {
	plugin := "host-local"
	path := "/opt/cni/bin"
	pluginPath := fmt.Sprintf("%s/%s", path, plugin)
	cmd := exec.Command(pluginPath)
	cmd.Env = append(os.Environ(),
		"CNI_COMMAND="+cnicommand, // ADD or DEL
		"CNI_CONTAINERID="+containerid,
		"CNI_NETNS="+netns,
		"CNI_IFNAME="+ifname,
		"CNI_PATH=/opt/cni/bin",
	)
	cmd.Stdin = strings.NewReader(config)
	// set output
	var out bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &stderr
	// cmdRun
	err := cmd.Run()
	if err != nil {
		return nil, fmt.Errorf("failed to call plugin %s: %v, stderr: %s", plugin, err, stderr.String())
	}
	return out.Bytes(), nil
}

func getIpamNetData(config map[string]interface{}) (string, string, string, error) {
	ips, ok := config["ips"].([]interface{})
	if !ok || len(ips) == 0 {
		return "", "", "", fmt.Errorf("campo 'ips' mancante")
	}
	ipEntry, ok := ips[0].(map[string]interface{})
	if !ok {
		return "", "", "", fmt.Errorf("il primo elemento di 'ips' non è del tipo atteso map[string]interface{}")
	}
	address, ok := ipEntry["address"].(string)
	if !ok {
		return "", "", "", fmt.Errorf("campo 'address' mancante")
	}
	gateway, ok := ipEntry["gateway"].(string)
	if !ok {
		return "", "", "", fmt.Errorf("campo 'gateway' mancante")
	}
	parts := strings.Split(address, "/")
	if len(parts) != 2 {
		return "", "", "", fmt.Errorf("formato 'address' non valido: %s", address)
	}
	ip := parts[0]
	cidr := parts[1]
	return ip, cidr, gateway, nil
}

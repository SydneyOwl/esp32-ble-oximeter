// pages/spo2/spo2.js
import * as echarts from '../ec-canvas/echarts';
var showTime = [];
var showSpo2 = [];
var showBPM = []; //60 data
var count = 0;
var chartd = null;
var timer = null;

function setOptions() {
  var option = {
    title: {
      text: 'SpO2 and BPM',
      left: 'center'
    },
    grid: {
      containLabel: true
    },
    legend: {
      data: ['spo2', 'bpm'],
      top: 30,
      left: 'center',
      z: 100
    },
    tooltip: {
      show: true,
      trigger: 'axis'
    },
    xAxis: {
      type: 'category',
      boundaryGap: false,
      data: showTime,
      // show: false
    },
    yAxis: {
      x: 'center',
      type: 'value',
      splitLine: {
        lineStyle: {
          type: 'dashed'
        }
      }
      // show: false
    },
    series: [{
      name: 'spo2',
      type: 'line',
      smooth: true,
      data: showSpo2
    }, {
      name: 'bpm',
      type: 'line',
      smooth: true,
      data: showBPM
    }]
  };
  chartd.setOption(option);
}

function initChart(canvas, width, height, dpr) { //初始化第一个图表
  const chart = echarts.init(canvas, null, {
    width: width,
    height: height,
    devicePixelRatio: dpr // new
  });
  chartd = chart;
  canvas.setChart(chart)
  setOptions(chart)
  return chart;
}
Page({
  /**
   * Page initial data
   */
  data: {
    ec: {
      onInit: initChart
    },
    graphDot: 60,
    pollingBLE: false,
    adapterOpened: false,
    startDiscovery: false,
    deviceId: null,
    serviceId: null,
    characteristicId: null,
    latestMsg: {},
    deviceData: {},
    tabs: ['数据', '设备信息', '设置'],
    tabIndex: 0,
    updateInterval: 2000
  },
  onTabClick(e) {
    let id = e.currentTarget.id;
    this.setData({
      tabIndex: id,
    })
  },
  // ble funcs
  InitBLE(event) {
    var that = this;
    if (that.data.adapterOpened) {
      console.log("蓝牙适配器已打开,请勿重复操作------》");
      return;
    }
    wx.openBluetoothAdapter({
      mode: "central",
      success: function (res) {
        console.log('初始化蓝牙适配器成功')
        that.setData({
          adapterOpened: true
        })
        wx.getBluetoothAdapterState({
          success: function (res) { //打印相关信息        
            console.log("蓝牙是否可用：" + res.available);
            that.SearchBLE()
          },
          fail: function (res) { //打印相关信息        
            console.log("蓝牙是否可用：" + res.available);
          }
        })
      },
      fail: function (res) {
        console.log('请打开蓝牙和定位功能')
        that.setData({
          adapterOpened: false
        })
        wx.showToast({
          title: '请开启手机蓝牙',
          icon: 'none',
          duration: 1000
        })
        setTimeout(()=>{
          wx.navigateBack({
            delta: 1
          })
        },1000)
      }
    })
  },
  SearchBLE(event) {
    var that = this;
    if (that.data.startDiscovery) {
      console.log("已开启蓝牙扫描，勿重复开启-----------》");
      return;
    } else {
      that.setData({
        startDiscovery: true
      })
      wx.startBluetoothDevicesDiscovery({
        allowDuplicatesKey: false,
        success: function (res) {
          wx.showLoading({
            title: '正在搜索设备',
          })
          that.onBluetoothDeviceFound();
        },
        fail: (res) => {
          that.setData({
            startDiscovery: false
          })
        },
      })
    }
  },
  onBluetoothDeviceFound() {
    var that = this;
    this.findTimer = setTimeout(() => {
      clearTimeout(this.findTimer);
      wx.hideLoading()
      console.log("蓝牙扫描超时，自动关闭任务-------------》");
      that.setData({
        startDiscovery: false
      })
      wx.stopBluetoothDevicesDiscovery();
      that.getAllBleDevs()
    }, 5000);
    // 监听扫描
  },

  getAllBleDevs() {
    var that = this
    var found = false;
    wx.getBluetoothDevices({
      success: (res) => {
        for (var i = 0; i < res.devices.length; i++) {
          if (res.devices[i].localName != "OWL血氧仪") {
            continue
          } else {
            found = true
            console.log(res.devices[i])
            wx.showLoading({
              title: '连接设备...',
            })
            that.createBleDeviceConnection(res.devices[i].deviceId)
            break;
          }
        }
        if (!found) {
          wx.showToast({
            title: '未找到设备！',
            icon: 'error',
            duration: 2000
          })
          setTimeout(()=>{
            wx.navigateBack({
              delta: 1
            })
          },1000)
        }
      }
    })
  },
  createBleDeviceConnection(deviceId) {
    const that = this;
    wx.closeBLEConnection({
      deviceId: deviceId,
    });
    console.log("开始连接蓝牙------------》", deviceId);
    // this.stopBLEDevicesTask();
    wx.createBLEConnection({
      deviceId: deviceId,
      timeout: 5000,
      success: (res) => {
        console.log("蓝牙连接成功----------》", deviceId);
        if (wx.getSystemInfoSync().platform == "android") {
          wx.setBLEMTU({
            deviceId: deviceId,
            mtu: 512,
            success: () => {
              console.log("成功")
            },
            fail: () => {
              console.log("失败")
            }
          })
        }
        // 获取特征和服务
        that.getBLEServices(deviceId)
      },
      fail: (res) => {
        console.log("连接失败-------》", res.errMsg);
        wx.showToast({
          title: '失败',
          icon: "fail",
          duration: 2000
        })
      },
    });
  },
  getBLEServices(deviceId) {
    var that = this;
    // UUID: cdfa000d-a7b4-4aab-aabe-4e81a362188a
    // 其实没必要就是了
    wx.getBLEDeviceServices({
      deviceId: deviceId,
      success: (res) => {
        for (let i = 0; i < res.services.length; i++) {
          if (res.services[i].uuid == "CDFA000D-A7B4-4AAB-AABE-4E81A362188A") {
            console.log("Found Service!")
            that.getBleCharacteristics(deviceId, res.services[i].uuid)
          }
        }
      },
      fail: () => {
        wx.hideLoading()
        wx.showToast({
          title: '失败！',
          icon: "fail",
          duration: 2000
        })
      }
    })
  },
  getBleCharacteristics(deviceId, serviceId) {
    var that = this;
    wx.getBLEDeviceCharacteristics({
      deviceId: deviceId,
      serviceId: serviceId,
      success: (res) => {
        console.log("Found characteristic")
        console.log(res.characteristics[0].properties)
        that.setData({
          deviceId: deviceId,
          serviceId: serviceId,
          characteristicId: res.characteristics[0].uuid
        })
        wx.notifyBLECharacteristicValueChange({
          deviceId: deviceId,
          serviceId: serviceId,
          characteristicId: res.characteristics[0].uuid,
          state: true,
          fail: (res) => {
            console.log(res)
          }
        });
        wx.onBLECharacteristicValueChange((result) => {
          var raw = String.fromCharCode.apply(null, new Uint8Array(result.value));
          console.log(raw)
          var res = JSON.parse(raw);
          if (res.BPM == undefined) {
            that.setData({
              deviceData: res
            })
          } else {
            console.log("branch in")
            res.BPM = res.BPM.toFixed(0)
            res.SpO2 = res.SpO2.toFixed(1)
            if (res.SpO2 == 80.0) {
              res.BPM = "---"
              res.SpO2 = '---'
            } else {
              while (showBPM.length > that.data.graphDot) {
                showBPM.shift()
                showSpo2.shift()
                showTime.shift()
              }
              showBPM.push(res.BPM)
              showSpo2.push(res.SpO2)
              showTime.push(count++)
              setOptions()
            }
            that.setData({
              latestMsg: res
            })
          }
          console.log(`Value changed: ${raw}`)
        })
        wx.hideLoading()
        wx.showToast({
          title: '成功！',
          icon: "success",
          duration: 2000
        })
        //--
        // that.fetchSensorData()
        that.fetchDeviceData()
        that.PollSensor()
        /**/
      },
      fail: () => {
        wx.hideLoading()
        wx.showToast({
          title: '失败！',
          icon: "fail",
          duration: 2000
        })
      }
    })
  },
  PollSensor() {
    var that = this;
    timer = setTimeout(function () {
      console.log("poll")
      that.fetchSensorData()
      that.PollSensor()
    }, that.data.updateInterval)
  },
  Uint8ArrayToString(fileData) {
    var dataString = "";
    for (var i = 0; i < fileData.length; i++) {
      dataString += String.fromCharCode(fileData[i]);
    }
    return dataString
  },
  stringToBytes(str) {
    var array = new Uint8Array(str.length);
    for (var i = 0, l = str.length; i < l; i++) {
      array[i] = str.charCodeAt(i);
    }
    return array.buffer;
  },
  fetchSensorData() {
    var that = this
    wx.writeBLECharacteristicValue({
      characteristicId: that.data.characteristicId,
      deviceId: that.data.deviceId,
      serviceId: that.data.serviceId,
      value: that.stringToBytes("{\"order\":\"getData\"}"),
      fail: (res) => {
        console.log(res)
      }
    })
  },
  fetchDeviceData() {
    var that = this
    wx.writeBLECharacteristicValue({
      characteristicId: that.data.characteristicId,
      deviceId: that.data.deviceId,
      serviceId: that.data.serviceId,
      value: that.stringToBytes("{\"order\":\"getDeviceInfo\"}"),
      fail: (res) => {
        console.log(res)
      }
    })
  },
  /**
   * Lifecycle function--Called when page load
   */
  onLoad(options) {},

  /**
   * Lifecycle function--Called when page is initially rendered
   */
  onReady() {},

  /**
   * Lifecycle function--Called when page show
   */
  onShow() {
    this.InitBLE()
  },

  /**
   * Lifecycle function--Called when page hide
   */
  onHide() {

  },

  /**
   * Lifecycle function--Called when page unload
   */
  onUnload() {
    clearTimeout(timer)
    this.startDiscovery = false;
    wx.stopBluetoothDevicesDiscovery();
    // 关闭扫描新设备监听
    wx.offBluetoothDeviceFound();
    // 关闭数据监听
    wx.offBLECharacteristicValueChange();
    // 移除蓝牙低功耗连接状态改变事件的监听函数
    wx.offBLEConnectionStateChange();
    wx.closeBluetoothAdapter()
  },

  /**
   * Page event handler function--Called when user drop down
   */
  onPullDownRefresh() {

  },

  /**
   * Called when page reach bottom
   */
  onReachBottom() {

  },

  /**
   * Called when user click on the top right corner to share
   */
  onShareAppMessage() {

  },
  tapUpdate() {
    console.log("tapped")
    this.fetchDeviceData()
  }
  //   Value changed: {"status":200,"millis":15309,"BPM":82.38755419,"SpO2":80,"ir_forGraph":90.41027069,"red_forGraph":89.97265625,"ir":1945,"red":2089}
  //   Value changed: {"millis":15893,"compilationDate":"Aug  3 2024 11:55:40","chipModel":"ESP32-D0WDQ6","chipRevision":1,"cpuFreqMHz":240,"chipCores":2,"heapSizeKiB":256,"freeHeapKiB":134,"psramSizeKiB":0,"freePsramKiB":0,"flashChipId":1466568,"flashSpeedMHz":40,"flashSizeMib":4,"sketchMD5":"43680918243821623a92a82dc15e3877","sdkVersion":"v4.4.7-dirty"}
})
const fs = require('fs');
const ffmpeg = require('./build/Release/ffmpeg');
const {Video} = ffmpeg

const v = new Video();
const d = fs.readFileSync('./sample.mpeg');
v.load(d);

console.log(v.currentTime, v.duration);

v.currentTime = 5;
v.update();

console.log(v.currentTime, v.duration);

module.exports = ffmpeg;

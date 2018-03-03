const fs = require('fs');
const ffmpeg = require('./build/Release/ffmpeg');
const {Video} = ffmpeg

const v = new Video();
const encodedData = fs.readFileSync('./sample.mpeg');
v.load(encodedData);

console.log(v.width, v.height);
console.log(v.currentTime, v.duration);

v.currentTime = 10;

console.log(v.currentTime, v.duration);

console.log(v.data.slice(v.data.length / 2, v.data.length / 2 + 4));

module.exports = ffmpeg;

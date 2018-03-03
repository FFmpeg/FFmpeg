const fs = require('fs');
const ffmpeg = require('./build/Release/ffmpeg');
const {Video} = ffmpeg

const v = new Video();
const d = fs.readFileSync('./sample.mpeg');
v.load(d);

module.exports = ffmpeg;

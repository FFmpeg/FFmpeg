/* Variables */
.root {
    --ff-colvideo: #6eaa7b;
    --ff-colaudio: #477fb3;
    --ff-colsubtitle: #ad76ab;
    --ff-coltext: #666;
}

/* Common & Misc */
.ff-inputfiles rect, .ff-outputfiles rect, .ff-inputstreams rect, .ff-outputstreams rect, .ff-decoders rect, .ff-encoders rect {
    stroke-width: 0;
    stroke: transparent;
    filter: none !important;
    fill: transparent !important;
    display: none !important;
}

.cluster span {
    color: var(--ff-coltext);
}

.cluster rect {
    stroke: #dfdfdf !important;
    transform: translateY(-2.3rem);
    filter: drop-shadow(1px 2px 2px rgba(185,185,185,0.2)) !important;
    rx: 8;
    ry: 8;
}

.cluster-label {
    font-size: 1.1rem;
}

    .cluster-label .nodeLabel {
        display: block;
        font-weight: 500;
        color: var(--ff-coltext);
    }

    .cluster-label div {
        max-width: unset !important;
        padding: 3px;
    }

    .cluster-label foreignObject {
        transform: translateY(-0.7rem);
    }

/* Input and output files */
.node.ff-inputfile .label foreignObject, .node.ff-outputfile .label foreignObject {
    overflow: visible;
}

.cluster.ff-inputfile .cluster-label foreignObject div:not(foreignObject div div), .cluster.ff-outputfile .cluster-label foreignObject div:not(foreignObject div div) {
    display: table !important;
}

.nodeLabel div.ff-inputfile, .nodeLabel div.ff-outputfile {
    font-size: 1.1rem;
    font-weight: 500;
    min-width: 14rem;
    width: 100%;
    display: flex;
    color: var(--ff-coltext);
    margin-top: 0.1rem;
    line-height: 1.35;
    padding-bottom: 1.9rem;
}

.nodeLabel div.ff-outputfile {
    flex-direction: row-reverse;
}

.ff-inputfile .index, .ff-outputfile .index {
    order: 2;
    color: var(--ff-coltext);
    text-align: center;
    border-radius: 0.45rem;
    border: 0.18em solid #666666db;
    font-weight: 600;
    padding: 0 0.3em;
    opacity: 0.8;
}

    .ff-inputfile .index::before {
        content: 'In ';
    }

    .ff-outputfile .index::before {
        content: 'Out ';
    }

.ff-inputfile .demuxer_name, .ff-outputfile .muxer_name {
    flex: 1;
    order: 1;
    font-size: 0.9rem;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    text-align: center;
    max-width: 8rem;
    align-content: center;
    margin: 0.2rem 0.4rem 0 0.4rem;
}

.ff-inputfile .file_extension, .ff-outputfile .file_extension {
    order: 0;
    background-color: #888;
    color: white;
    text-align: center;
    border-radius: 0.45rem;
    font-weight: 600;
    padding: 0 0.4em;
    align-content: center;
    opacity: 0.8;
}

.ff-inputfile .url, .ff-outputfile .url {
    order: 4;
    text-align: center;
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0.75rem;
    font-size: 0.7rem;
    font-weight: 400;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    margin: 0 0.3rem;
    direction: rtl;
    color: #999;
}

.cluster.ff-inputfile rect, .cluster.ff-outputfile rect {
    transform: translateY(-1.8rem);
    fill: url(#ff-radgradient);
}

/* Input and output streams */
.node.ff-inputstream rect, .node.ff-outputstream rect {
    padding: 0 !important;
    margin: 0 !important;
    border: none !important;
    fill: white;
    stroke: #e5e5e5 !important;
    height: 2.7rem;
    transform: translateY(0.2rem);
    filter: none;
    rx: 3;
    ry: 3;
}

.node.ff-inputstream .label foreignObject, .node.ff-outputstream .label foreignObject {
    transform: translateY(-0.2%);
    overflow: visible;
}

    .node.ff-inputstream .label foreignObject div:not(foreignObject div div), .node.ff-outputstream .label foreignObject div:not(foreignObject div div) {
        display: block !important;
        line-height: 1.5 !important;
    }

.nodeLabel div.ff-inputstream, .nodeLabel div.ff-outputstream {
    font-size: 1.0rem;
    font-weight: 500;
    min-width: 12rem;
    width: 100%;
    display: flex;
}

.nodeLabel div.ff-outputstream {
    flex-direction: row-reverse;
}

.ff-inputstream .name, .ff-outputstream .name {
    flex: 1;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    text-align: left;
    align-content: center;
    margin-bottom: -0.15rem;
}

.ff-inputstream .index, .ff-outputstream .index {
    flex: 0 0 1.4rem;
    background-color: #888;
    color: white;
    text-align: center;
    border-radius: 0.3rem;
    font-weight: 600;
    margin-right: -0.3rem;
    margin-left: 0.4rem;
    opacity: 0.8;
}

.ff-outputstream .index {
    margin-right: 0.6rem;
    margin-left: -0.4rem;
}

.ff-inputstream::before, .ff-outputstream::before {
    font-variant-emoji: text;
    flex: 0 0 2rem;
    margin-left: -0.8rem;
    margin-right: 0.2rem;
}

.ff-outputstream::before {
    margin-left: 0.2rem;
    margin-right: -0.6rem;
}

.ff-inputstream.video::before, .ff-outputstream.video::before {
    content: '\239A';
    color: var(--ff-colvideo);
    font-size: 2.25rem;
    line-height: 0.5;
    font-weight: bold;
}

.ff-inputstream.audio::before, .ff-outputstream.audio::before {
    content: '\1F39D';
    color: var(--ff-colaudio);
    font-size: 1.75rem;
    line-height: 0.9;
}

.ff-inputstream.subtitle::before, .ff-outputstream.subtitle::before {
    content: '\1AC';
    color: var(--ff-colsubtitle);
    font-size: 1.2rem;
    line-height: 1.1;
    transform: scaleX(1.5);
    margin-top: 0.050rem;
}

.ff-inputstream.attachment::before, .ff-outputstream.attachment::before {
    content: '\1F4CE';
    font-size: 1.3rem;
    line-height: 1.15;
}

.ff-inputstream.data::before, .ff-outputstream.data::before {
    content: '\27E8\2219\2219\2219\27E9';
    font-size: 1.15rem;
    line-height: 1.17;
    letter-spacing: -0.3px;
}

/* Filter Graphs */
.cluster.ff-filters rect {
    stroke-dasharray: 6 !important;
    stroke-width: 1.3px;
    stroke: #d1d1d1 !important;
    filter: none !important;
}

.cluster.ff-filters div.ff-filters .id {
    display: none;
}

.cluster.ff-filters div.ff-filters .name {
    margin-right: 0.5rem;
    font-size: 0.9rem;
}

.cluster.ff-filters div.ff-filters .description {
    font-weight: 400;
    font-size: 0.75rem;
    vertical-align: middle;
    color: #777;
    font-family: Cascadia Code, Lucida Console, monospace;
}

/* Filter Shapes */
.node.ff-filter rect {
    rx: 10;
    ry: 10;
    stroke-width: 1px;
    stroke: #d3d3d3;
    fill: url(#ff-filtergradient);
    filter: drop-shadow(1px 1px 2px rgba(0, 0, 0, 0.1));
}

.node.ff-filter .label foreignObject {
    transform: translateY(-0.4rem);
    overflow: visible;
}

.nodeLabel div.ff-filter {
    font-size: 1.0rem;
    font-weight: 500;
    text-transform: uppercase;
    min-width: 5.5rem;
    margin-bottom: 0.5rem;
}

    .nodeLabel div.ff-filter span {
        color: inherit;
    }

/* Decoders & Encoders */
.node.ff-decoder rect, .node.ff-encoder rect {
    stroke-width: 1px;
    stroke: #d3d3d3;
    fill: url(#ff-filtergradient);
    filter: drop-shadow(1px 1px 2px rgba(0, 0, 0, 0.1));
}

.nodeLabel div.ff-decoder, .nodeLabel div.ff-encoder {
    font-size: 0.85rem;
    font-weight: 500;
    min-width: 3.5rem;
}

/* Links and Arrows */
path.flowchart-link[id|='video'] {
    stroke: var(--ff-colvideo);
}

path.flowchart-link[id|='audio'] {
    stroke: var(--ff-colaudio);
}

path.flowchart-link[id|='subtitle'] {
    stroke: var(--ff-colsubtitle);
}

marker.marker path {
    fill: context-stroke;
}

.edgeLabel foreignObject {
    transform: translateY(-1rem);
}

.edgeLabel p {
    background: transparent;
    white-space: nowrap;
    margin: 1rem 0.5rem !important;
    font-weight: 500;
    color: var(--ff-coltext);
}

.edgeLabel, .labelBkg {
    background: transparent;
}

.edgeLabels .edgeLabel * {
    font-size: 0.8rem;
}

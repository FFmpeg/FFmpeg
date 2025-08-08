module.exports = async ({github, context}) => {
    const title = (context.payload.pull_request?.title || context.payload.issue?.title || '').toLowerCase();
    const labels = [];
    const issueNumber = context.payload.pull_request?.number || context.payload.issue?.number;

    async function isOrgMember(username) {
        try {
            const response = await github.rest.orgs.checkMembershipForUser({
                org: context.repo.owner,
                username: username
            });
            return response.status === 204;
        } catch (error) {
            return false;
        }
    }

    var removeNew = context.payload.action === 'closed';

    if (context.payload.action !== 'opened' && await isOrgMember(context.payload.sender.login)) {
        if (context.payload.action === 'assigned' ||
            context.payload.action === 'labeled' ||
            context.payload.action === 'unlabeled' ||
            context.payload.comment) {
            removeNew = true;
            console.log('Removing "new" label due to member interaction.');
        }
    }

    if (removeNew) {
        try {
            await github.rest.issues.removeLabel({
                owner: context.repo.owner,
                repo: context.repo.repo,
                issue_number: issueNumber,
                name: 'new'
            });
            console.log('Removed "new" label');
        } catch (error) {
            console.log('Could not remove "new" label');
        }
    } else if (context.payload.action === 'opened') {
        labels.push('new');
        console.log('Detected label: new');
    }

    if (context.payload.action === 'opened' || context.payload.action === 'edited') {
        const kwmap = {
          'avcodec': 'avcodec',
          'avdevice': 'avdevice',
          'avfilter': 'avfilter',
          'avformat': 'avformat',
          'avutil': 'avutil',
          'swresample': 'swresample',
          'swscale': 'swscale',
          'fftools': 'CLI'
        };

        for (const [kw, label] of Object.entries(kwmap)) {
            if (title.includes(kw)) {
                labels.push(label);
                console.log('Detected label: ' + label);
            }
        }
    }

    if (labels.length > 0) {
        await github.rest.issues.addLabels({
            owner: context.repo.owner,
            repo: context.repo.repo,
            issue_number: issueNumber,
            labels: labels,
        });
    }
}
